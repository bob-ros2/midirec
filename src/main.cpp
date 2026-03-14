#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <sstream>
#include "RtMidi.h"

// Constants
const int DEFAULT_PPQ = 480;
const int DEFAULT_BPM = 120;
const double DEFAULT_SILENCE_TIMEOUT = 10.0; // seconds

// Global/Shared State
struct MidiEvent {
    double deltaTime;
    std::vector<unsigned char> message;
};

std::vector<MidiEvent> g_buffer;
std::mutex g_bufferMutex;
std::atomic<bool> g_recording(false);
std::atomic<double> g_lastMessageTime(0.0);
std::atomic<bool> g_exit(false);
double g_accumulatedDelta = 0.0; // Accessed only in the callback thread

// Helper to write Variable Length Quantity (VLQ) for MIDI files
void writeVLQ(std::ostream& out, uint32_t value) {
    uint8_t bytes[4];
    int count = 0;
    bytes[count++] = static_cast<uint8_t>(value & 0x7F);
    while (value >>= 7) {
        bytes[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    }
    while (count > 0) {
        out.put(static_cast<char>(bytes[--count]));
    }
}

// Helper to write big-endian values
void writeBE(std::ostream& out, uint32_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) {
        out.put(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

void saveMidiFile(const std::vector<MidiEvent>& events, int bpm, int ppq) {
    if (events.empty()) return;

    // Generate filename: YYYYMMDD_HH24MISS_record.mid
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_struct;
    localtime_s(&tm_struct, &now);
    std::stringstream ss;
    ss << std::put_time(&tm_struct, "%Y%m%d_%H%M%S") << "_record.mid";
    std::string filename = ss.str();

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Could not open file for writing: " << filename << std::endl;
        return;
    }

    // MThd Header
    out.write("MThd", 4);
    writeBE(out, 6, 4); // length
    writeBE(out, 0, 2); // format 0 (single track)
    writeBE(out, 1, 2); // number of tracks
    writeBE(out, ppq, 2); // ticks per quarter note

    // MTrk Header (we need to buffer track content to calculate length)
    std::stringstream trackData;
    
    // Set Tempo: 500,000 microseconds per quarter note = 120 BPM
    uint32_t microsecondsPerQuarter = 60000000 / bpm;
    trackData.put(0x00); // delta 0
    trackData.put(static_cast<char>(0xFF));
    trackData.put(0x51);
    trackData.put(0x03);
    writeBE(trackData, microsecondsPerQuarter, 3);

    // Set Time Signature: 4/4
    // FF 58 04 nn dd cc bb
    // nn: numerator (4)
    // dd: denominator power (2^2 = 4)
    // cc: clocks per click (24)
    // bb: 32nd notes per 24 clocks (8)
    trackData.put(0x00); // delta 0
    trackData.put(static_cast<char>(0xFF));
    trackData.put(0x58);
    trackData.put(0x04);
    trackData.put(0x04); // 4
    trackData.put(0x02); // /4
    trackData.put(0x18); // 24
    trackData.put(0x08); // 8

    // Delta time calculation:
    // RtMidi provides delta time in seconds.
    // Ticks = DeltaSeconds * (BPM / 60) * PPQ
    double ticksPerSecond = (static_cast<double>(bpm) / 60.0) * static_cast<double>(ppq);

    for (const auto& ev : events) {
        uint32_t deltaTicks = static_cast<uint32_t>(ev.deltaTime * ticksPerSecond + 0.5);
        writeVLQ(trackData, deltaTicks);
        for (unsigned char b : ev.message) {
            trackData.put(static_cast<char>(b));
        }
    }

    // End of Track
    trackData.put(0x00); // delta 0
    trackData.put(static_cast<char>(0xFF));
    trackData.put(0x2F);
    trackData.put(0x00);

    std::string data = trackData.str();
    out.write("MTrk", 4);
    writeBE(out, static_cast<uint32_t>(data.size()), 4);
    out.write(data.data(), data.size());

    std::cout << "Saved: " << filename << " (" << events.size() << " events)" << std::endl;
}

void midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->empty()) return;

    g_accumulatedDelta += deltatime;

    // Ignore Active Sensing (0xFE) and Clock (0xF8)
    if (message->at(0) == 0xFE || message->at(0) == 0xF8) return;

    // Capture the accumulated delta for this message
    double deltaToRecord = g_accumulatedDelta;
    g_accumulatedDelta = 0.0;

    double currentTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    g_lastMessageTime = currentTime;

    if (!g_recording) {
        std::cout << "Input detected! Starting recording..." << std::endl;
        g_recording = true;
        std::lock_guard<std::mutex> lock(g_bufferMutex);
        g_buffer.clear();
        g_buffer.push_back({0.0, *message}); // First message always gets delta 0
    } else {
        std::lock_guard<std::mutex> lock(g_bufferMutex);
        g_buffer.push_back({deltaToRecord, *message});
    }
}

void listDevices(RtMidiIn& midiIn) {
    unsigned int nPorts = midiIn.getPortCount();
    std::cout << "\nAvailable MIDI input ports:\n";
    for (unsigned int i = 0; i < nPorts; i++) {
        std::cout << "  [" << i << "] " << midiIn.getPortName(i) << std::endl;
    }
    std::cout << std::endl;
}

void printUsage() {
    std::cout << "Usage: midirec [options]\n"
              << "Options:\n"
              << "  -l, --list           Zeigt alle verfügbaren MIDI-Eingabegeräte an\n"
              << "  -i, --index <id>     Wählt ein spezifisches Gerät über den Index aus (Standard: 0)\n"
              << "  -t, --timeout <s>    Setzt die Inaktivitätszeit in Sekunden (Standard: 10)\n"
              << "  --bpm <val>          BPM für das Ausgabeformat (Standard: 120)\n"
              << "  -h, --help           Zeigt diese Hilfe an\n";
}

int main(int argc, char* argv[]) {
    unsigned int portIndex = 0;
    double silenceTimeout = DEFAULT_SILENCE_TIMEOUT;
    int bpm = DEFAULT_BPM;
    bool portSpecified = false;

    RtMidiIn midiIn;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-l" || arg == "--list") {
            listDevices(midiIn);
            return 0;
        } else if ((arg == "-i" || arg == "--index" || arg == "--port") && i + 1 < argc) {
            portIndex = std::stoi(argv[++i]);
            portSpecified = true;
        } else if ((arg == "-t" || arg == "--timeout") && i + 1 < argc) {
            silenceTimeout = std::stod(argv[++i]);
        } else if (arg == "--bpm" && i + 1 < argc) {
            bpm = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
    }

    if (midiIn.getPortCount() == 0) {
        std::cerr << "Error: No MIDI input sources found." << std::endl;
        return 1;
    }

    if (portIndex >= midiIn.getPortCount()) {
        std::cerr << "Error: Port index " << portIndex << " out of range." << std::endl;
        listDevices(midiIn);
        return 1;
    }

    try {
        midiIn.openPort(portIndex);
        midiIn.setCallback(&midiCallback);
        midiIn.ignoreTypes(false, false, false); // Don't ignore sysEx, time, sensing
    } catch (RtMidiError& error) {
        std::cerr << "Error: " << error.getMessage() << std::endl;
        return 1;
    }

    std::cout << "Monitoring " << midiIn.getPortName(portIndex) << "..." << std::endl;
    std::cout << "Press Ctrl+C to exit. Recording starts on first message." << std::endl;

    while (!g_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (g_recording) {
            double currentTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (currentTime - g_lastMessageTime > silenceTimeout) {
                std::cout << "Silence detected (" << silenceTimeout << "s). Saving track..." << std::endl;
                
                std::vector<MidiEvent> toSave;
                {
                    std::lock_guard<std::mutex> lock(g_bufferMutex);
                    toSave = std::move(g_buffer);
                    g_buffer.clear();
                    g_recording = false;
                }
                
                saveMidiFile(toSave, bpm, DEFAULT_PPQ);
                std::cout << "Reset. Waiting for next input..." << std::endl;
            }
        }
    }

    return 0;
}
