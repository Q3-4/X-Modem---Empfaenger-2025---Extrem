/*
  Hinweis zu Zeichensatz-Einstellungen (Visual Studio):
    Projekt > Eigenschaften > Konfigurationseigenschaften > Erweitert > Zeichensatz
      - Unicode-Zeichensatz (Standard, nutzt CreateFileW)
      - Multi-Byte-Zeichensatz (ANSI, nutzt CreateFileA)

  Diese Klasse nutzt std::string (ANSI), funktioniert aber auch mit Unicode
  (Visual Studio konvertiert automatisch).
  Empfohlen: Plattform "x64".
*/

#include "Serial.h"

// ==============================
// Konstruktor / Destruktor
// ==============================

Serial::Serial(string portName, int baudrate, int dataBits, int stopBits, int parity)
{
    this->portName = portName;
    this->baudrate = baudrate;
    this->dataBits = dataBits;
    this->stopBits = stopBits;
    this->parity = parity;
    this->handle = INVALID_HANDLE_VALUE;
}

Serial::~Serial()
{
    close();
}

// ==============================
// Öffnen / Schließen
// ==============================
    bool Serial::open()
    {
        // COM-Port öffnen (synchron, ohne Overlapped I/O)
    #ifdef UNICODE
        // Konvertiere std::string (ANSI) nach std::wstring (UTF-16)
        std::wstring wPortName(portName.begin(), portName.end());
        handle = CreateFile(
            wPortName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,                // kein Sharing
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    #else
        handle = CreateFile(
            portName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,                // kein Sharing
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    #endif

        if (handle == INVALID_HANDLE_VALUE)
            return false;

        // Port-Parameter holen und anpassen
        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);

        if (!GetCommState(handle, &dcb)) {
            close();
            return false;
        }

        dcb.BaudRate = baudrate;
        dcb.ByteSize = (BYTE)dataBits;
        dcb.StopBits = (BYTE)stopBits;
        dcb.Parity = (BYTE)parity;
        dcb.fParity = (dcb.Parity != NOPARITY);

        if (!SetCommState(handle, &dcb)) {
            close();
            return false;
        }

        // --- Klassisch/blockierend: ReadFile wartet, bis Daten anliegen ----------
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = 0;
        to.ReadTotalTimeoutMultiplier = 0;
        to.ReadTotalTimeoutConstant = 0;   // 0 => blockiert (kein globales Read-Timeout)
        to.WriteTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant = 100;

        if (!SetCommTimeouts(handle, &to)) {
            close();
            return false;
        }
        // -------------------------------------------------------------------------

        return true;
    }

void Serial::close(void)
{
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

// ==============================
// Verfügbarkeit / Lesen / Schreiben
// ==============================

int Serial::dataAvailable()
{
    COMSTAT comStat;
    DWORD errors;

    if (handle != INVALID_HANDLE_VALUE)
        if (ClearCommError(handle, &errors, &comStat))
            return (int)comStat.cbInQue;

    return 0;
}

void Serial::write(int value)
{
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        char v = (char)value;
        WriteFile(handle, &v, 1, &bytesWritten, nullptr);
    }
}

void Serial::write(const char* buffer, int bytesToWrite)
{
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(handle, buffer, bytesToWrite, &bytesWritten, nullptr);
    }
}

void Serial::write(string s)
{
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(handle, s.c_str(), (DWORD)s.length(), &bytesWritten, nullptr);
    }
}

int Serial::read()
{
    if (handle == INVALID_HANDLE_VALUE)
        return -1;

    DWORD dwRead = 0;
    unsigned char chRead = 0;

    // Blockiert, bis 1 Byte gelesen wurde (oder Fehler)
    if (!ReadFile(handle, &chRead, 1, &dwRead, nullptr))
        return -1; // I/O-Fehler

    if (dwRead != 1)
        return -1; // sollte bei blockierenden Timeouts nicht passieren

    return (int)chRead;
}

int Serial::read(char* buffer, int bufSize)
{
    if (handle == INVALID_HANDLE_VALUE || buffer == nullptr || bufSize <= 0)
        return 0;

    DWORD bytesRead = 0;
    int i = 0;

    // 1) Mindestens 1 Byte lesen (blockierend)
    if (!ReadFile(handle, &buffer[i], 1, &bytesRead, nullptr))
        return 0;                   // I/O-Fehler
    if (bytesRead != 1)
        return 0;                   // sollte nicht passieren (blockierend)

    i += 1;

    // 2) Jetzt alle SOFORT verfügbaren Bytes ohne weiteres Warten „mitnehmen“
    //    (Nur so viele anfordern, wie im Treiberpuffer gemeldet werden.)
    while (i < bufSize) {
        int avail = dataAvailable();
        if (avail <= 0) break;

        int want = min(avail, bufSize - i);
        DWORD got = 0;
        if (!ReadFile(handle, &buffer[i], (DWORD)want, &got, nullptr) || got == 0)
            break;

        i += (int)got;
    }

    return i;  // >=1
}

string Serial::readLine()
{
    const unsigned char LF = 0x0A; // Linefeed
    string result;

    if (handle == INVALID_HANDLE_VALUE)
        return result;

    // Klassisch: warte bis '\n' kommt
    while (true) {
        int ch = read();            // blockiert bis 1 Byte
        if (ch < 0) {
            // Fehler -> bisheriges Ergebnis zurückgeben
            return result;
        }
        if ((unsigned char)ch == LF) {
            // LF nicht übernehmen -> Zeile fertig
            return result;
        }
        result.push_back((char)ch);

        // Schutz gegen unbeabsichtigt „endlose“ Zeilen
        if (result.size() > 1 << 20) { // ~1 MB
            return result;
        }
    }
}

// ==============================
// Modem-/Handshake-Signale
// ==============================

void Serial::setRTS(bool arg)
{
    if (handle != INVALID_HANDLE_VALUE) {
        if (arg) EscapeCommFunction(handle, SETRTS);
        else     EscapeCommFunction(handle, CLRRTS);
    }
}

void Serial::setDTR(bool arg)
{
    if (handle != INVALID_HANDLE_VALUE) {
        if (arg) EscapeCommFunction(handle, SETDTR);
        else     EscapeCommFunction(handle, CLRDTR);
    }
}

bool Serial::isCTS()
{
    DWORD status = 0;
    GetCommModemStatus(handle, &status);
    return (status & MS_CTS_ON) != 0;
}

bool Serial::isDSR()
{
    DWORD status = 0;
    GetCommModemStatus(handle, &status);
    return (status & MS_DSR_ON) != 0;
}
