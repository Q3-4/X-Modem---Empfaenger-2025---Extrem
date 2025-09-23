#include <iostream>
#include <iomanip>
#include <string>
#include "Serial.h"
using namespace std;

// ---- Steuerzeichen (wie im Unterricht) ----
static const unsigned char SOH = 0x01; // Start Of Header
static const unsigned char ETX = 0x03; // End Of Text (Padding)
static const unsigned char EOT = 0x04; // End Of Transmission
static const unsigned char ACK = 0x06; // Acknowledge
static const unsigned char NAK = 0x15; // No Acknowledge
static const unsigned char CAN = 0x18; // Cancel (hier ungenutzt)

// ---- Block-Layout (vereinfachte Übung) ----
// | SOH | n | 255-n | Daten(5) | Checksum |
//    1     1    1        5          1   = 9
static const int DATABYTES = 5;
static const int BLOCKSIZE = 3 + DATABYTES + 1; // 9

// Prüfsumme: Summe der 5 Datenbytes mod 256
unsigned char checksum5(const unsigned char* data5) {
    int sum = 0;
    for (int i = 0; i < DATABYTES; ++i) sum += data5[i];
    return static_cast<unsigned char>(sum % 256);
}

// 1 Byte sicher lesen (blockierend, -1 => Fehler/geschlossen)
int readByte(Serial& com) {
    int b = com.read();            // read() blockiert bis 1 Byte kommt
    return b;                      // 0..255 oder -1
}

// Hilfs-Ausgabe für einen Block (hex & klar)
void dumpBlock(const unsigned char* blk) {
    cout << "Block (hex): ";
    for (int i = 0; i < BLOCKSIZE; ++i) {
        cout << "0x" << hex << uppercase << setw(2) << setfill('0')
            << static_cast<int>(blk[i]) << (i + 1 < BLOCKSIZE ? " " : "");
    }
    cout << dec << nouppercase << endl;
    cout << "  SOH=" << (int)blk[0]
        << " n=" << (int)blk[1]
        << " 255-n=" << (int)blk[2]
        << " chk=" << (int)blk[8] << endl;

    cout << "  Daten: ";
    for (int i = 0; i < DATABYTES; ++i) {
        unsigned char c = blk[3 + i];
        cout << (c >= 32 ? (char)c : '.'); // druckbar? sonst Punkt
    }
    cout << "'\n";
}

int main() {
    // --- COM-Port abfragen ---
    string portNr;
    cout << "COM Port Nummer: ";
    cin >> portNr;

    string port = "COM" + portNr;

    Serial com(port, 9600, 8, ONESTOPBIT, NOPARITY);

    if (!com.open()) {
        cout << "Fehler beim Öffnen von " << port << endl;
       
        return 1;
    }

    cout << "Empfaenger gestartet auf " << port << endl;
    cout << "Sende NAK (Empfaenger empfangsbereit) ..." << endl;
    com.write(NAK);

    string nachricht;              // sammelt reine Nutzdaten (ohne ETX)
    unsigned char erwarteterBlock = 1;

    while (true) {
        // Warte auf Beginn eines Blocks oder EOT
        int b0 = readByte(com);
        if (b0 < 0) { cout << "Lese-Fehler / Verbindung beendet." << endl; break; }

        unsigned char first = static_cast<unsigned char>(b0);

        if (first == EOT) {
            cout << "EOT erhalten - sende ACK ..." << endl;
            com.write(ACK);
            break;
        }

        if (first != SOH) {
            // Unerwartetes Byte (Rauschen o.ä.) - ignorieren, weiter warten.
            // (Bei echten Implementierungen könnte man hier robuster syncen.)
            continue;
        }

        // Wir haben ein SOH - restliche 8 Bytes des Blocks einlesen
        unsigned char blk[BLOCKSIZE];
        blk[0] = first;

        // n, 255-n, 5 Daten, checksum
        bool ioError = false;
        for (int i = 1; i < BLOCKSIZE; ++i) {
            int bi = readByte(com);
            if (bi < 0) { ioError = true; break; }
            blk[i] = static_cast<unsigned char>(bi);
        }
        if (ioError) {
            cout << "I/O-Fehler beim Lesen eines Blocks - sende NAK." << endl;
            com.write(NAK);
            continue;
        }

        cout << "\n--- Block empfangen ----------------------------------\n";
        dumpBlock(blk);

        // Kopf prüfen: n und 255-n
        unsigned char n = blk[1];
        unsigned char inv = blk[2];
        bool headerOk = (static_cast<unsigned char>(255 - n) == inv);

        if (!headerOk) {
            cout << "Header ungueltig (n/255-n passt nicht) - NAK." << endl;
            com.write(NAK);
            continue;
        }

        // Prüfsumme prüfen
        unsigned char calc = checksum5(&blk[3]);
        unsigned char got = blk[8];

        if (calc != got) {
            cout << "Checksumme falsch (calc=" << (int)calc
                << ", got=" << (int)got << ") - NAK." << endl;
            com.write(NAK);
            continue;
        }

        // Optional: Reihenfolge prüfen (für Unterricht hilfreich)
        if (n != erwarteterBlock) {
            cout << "Warnung: Unerwartete Blocknummer! Erwartet "
                << (int)erwarteterBlock << ", erhalten " << (int)n
                << ". (Sende trotzdem ACK - einfache Übungsversion.)" << endl;
            // In „streng“ könnte man NAK senden und denselben Block anfordern.
        }

        // Daten übernehmen (ETX-Füllbytes ignorieren)
        for (int i = 0; i < DATABYTES; ++i) {
            unsigned char c = blk[3 + i];
            if (c != ETX) nachricht.push_back(static_cast<char>(c));
        }

        cout << "Block OK - sende ACK." << endl;
        com.write(ACK);
        ++erwarteterBlock;
    }

    cout << "\n=============================================\n";
    cout << "Empfangene Nachricht: \"" << nachricht << "\"\n";
    cout << "=============================================\n";

    com.close();
   
    return 0;
}
