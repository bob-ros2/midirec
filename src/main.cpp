#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <RtMidi.h>
#include <MidiFile.h>

// ANSI Colors for premium look
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"

struct MidiEvent {
    double timestamp;
    std::vector<unsigned char> message;
};

class MidiRecorder {
public:
    MidiRecorder(int timeoutSec = 10) 
        : m_timeoutSec(timeoutSec), m_isRecording(false), m_shouldStop(false) {
        
        try {
            m_midiIn = std::make_unique<RtMidiIn>();
        } catch (RtMidiError &error) {
            error.printMessage();
            exit(EXIT_FAILURE);
        }
    }

    void listDevices() {
        unsigned int nPorts = m_midiIn->getPortCount();
        std::cout << "\n" << YELLOW << BOLD << "Verfügbare MIDI-Eingänge (Total: " << nPorts << "):" << RESET << "\n";
        for (unsigned int i = 0; i < nPorts; i++) {
            try {
                std::cout << "  [" << GREEN << i << RESET << "] " << WHITE << m_midiIn->getPortName(i) << RESET << "\n";
            } catch (RtMidiError &error) {
                error.printMessage();
            }
        }
        std::cout << std::endl;
    }

    bool openPort(int portIndex = -1) {
        unsigned int nPorts = m_midiIn->getPortCount();
        if (nPorts == 0) {
            std::cerr << "Keine MIDI-Eingabegeräte gefunden!\n";
            return false;
        }

        if (portIndex < 0) {
            std::cout << "Automatisches Öffnen von Port 0: " << m_midiIn->getPortName(0) << "\n";
            m_midiIn->openPort(0);
        } else {
            if (portIndex >= (int)nPorts) {
                std::cerr << "Port-Index " << portIndex << " ist ungültig.\n";
                return false;
            }
            std::cout << "Öffne Port " << portIndex << ": " << m_midiIn->getPortName(portIndex) << "\n";
            m_midiIn->openPort(portIndex);
        }

        // Sysex, Timing und Active Sensing ignorieren (wie gewünscht)
        m_midiIn->ignoreTypes(true, true, true);
        
        m_midiIn->setCallback(&MidiRecorder::midiCallback, this);
        return true;
    }

    void run() {
        std::cout << "\n" << CYAN << BOLD << ">>> Warte auf MIDI-Signale (Drücke eine Taste zum Starten)..." << RESET << std::endl;
        
        while (!m_shouldStop) {
            auto now = std::chrono::steady_clock::now();
            
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_isRecording) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastEventTime).count();
                if (elapsed >= m_timeoutSec) {
                    stopAndSave();
                    std::cout << "\n" << CYAN << BOLD << ">>> Warte erneut auf MIDI-Signale..." << RESET << std::endl;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void stop() {
        m_shouldStop = true;
    }

private:
    static void midiCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
        MidiRecorder* recorder = static_cast<MidiRecorder*>(userData);
        recorder->handleMessage(deltatime, *message);
    }

    void handleMessage(double deltatime, const std::vector<unsigned char>& message) {
        if (message.empty()) return;

        unsigned char status = message[0];
        unsigned char type = status & 0xF0;

        // Trigger: Note On (und Velocity > 0)
        if (!m_isRecording && type == 0x90 && message.size() > 2 && message[2] > 0) {
            startRecording();
        }

        if (m_isRecording) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastEventTime = std::chrono::steady_clock::now();
            
            // Absolute Zeit berechnen (MidiFile braucht Ticks oder Sekunden)
            if (m_sessionEvents.empty()) {
                m_sessionStartTime = std::chrono::steady_clock::now();
                m_currentAbsoluteTime = 0.0;
            } else {
                m_currentAbsoluteTime += deltatime;
            }
            
            m_sessionEvents.push_back({m_currentAbsoluteTime, message});
            
            // Kurzes Feedback in der Konsole
            std::cout << "." << std::flush;
        }
    }

    void startRecording() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isRecording = true;
        m_sessionEvents.clear();
        m_currentAbsoluteTime = 0.0;
        m_lastEventTime = std::chrono::steady_clock::now();
        
        std::cout << "\n" << RED << BOLD << "[REC] Aufnahme gestartet! (" << getTimestampString() << ")" << RESET << "\n Recording: " << std::flush;
    }

    void stopAndSave() {
        m_isRecording = false;
        
        if (m_sessionEvents.empty()) return;

        std::string filename = getTimestampString() + "_record.mid";
        std::cout << "\n" << GREEN << BOLD << "[SAVE] Aufnahme beendet. Datei: " << filename << " (" << m_sessionEvents.size() << " Events)" << RESET << std::endl;
        
        saveToFile(filename);
        m_sessionEvents.clear();
    }

    void saveToFile(const std::string& filename) {
        smf::MidiFile midifile;
        midifile.addTrack(1);
        int tpq = 480;
        midifile.setTPQ(tpq); // Ticks per quarter note

        // 120 BPM -> 2 Quarters per second -> 2 * TPQ ticks per second
        double ticksPerSecond = 2.0 * tpq;

        for (const auto& ev : m_sessionEvents) {
            int track = 0;
            int tick = static_cast<int>(ev.timestamp * ticksPerSecond);
            std::vector<unsigned char> messageCopy = ev.message;
            midifile.addEvent(track, tick, messageCopy);
        }

        midifile.sortTracks();
        midifile.write(filename);
    }

    std::string getTimestampString() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
        return ss.str();
    }

    std::unique_ptr<RtMidiIn> m_midiIn;
    int m_timeoutSec;
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_shouldStop;
    
    std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastEventTime;
    std::chrono::steady_clock::time_point m_sessionStartTime;
    double m_currentAbsoluteTime;
    std::vector<MidiEvent> m_sessionEvents;
};

void printHelp() {
    std::cout << "MidiRec - Ein einfacher Konsolen MIDI-Recorder\n\n"
              << "Benutzung:\n"
              << "  midirec [Optionen]\n\n"
              << "Optionen:\n"
              << "  -l, --list        Listet alle verfügbaren MIDI-Eingabegeräte auf\n"
              << "  -i, --index <id>  Verwendet das Gerät mit dem angegebenen Index (Standard: 0)\n"
              << "  -t, --timeout <s> Inaktivitäts-Zeitraum in Sekunden bis zum Speichern (Standard: 10)\n"
              << "  -h, --help        Zeigt diese Hilfe an\n"
              << std::endl;
}

int main(int argc, char** argv) {
    int portIndex = -1;
    int timeout = 10;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if (arg == "-l" || arg == "--list") {
            MidiRecorder recorder;
            recorder.listDevices();
            return 0;
        } else if (arg == "-i" || arg == "--index") {
            if (i + 1 < argc) {
                portIndex = std::stoi(argv[++i]);
            }
        } else if (arg == "-t" || arg == "--timeout") {
            if (i + 1 < argc) {
                timeout = std::stoi(argv[++i]);
            }
        }
    }

    std::cout << MAGENTA << BOLD << "========================================" << RESET << "\n";
    std::cout << MAGENTA << BOLD << "         MidiRec v1.0.0                 " << RESET << "\n";
    std::cout << MAGENTA << BOLD << "========================================" << RESET << "\n";
    
    MidiRecorder recorder(timeout);
    if (!recorder.openPort(portIndex)) {
        return EXIT_FAILURE;
    }

    recorder.run();

    return 0;
}
