#include <iostream>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <map>
#include <string>
#include <vector>

// Porty do wideo i audio
static const int VIDEO_PORT = 12345;
static const int AUDIO_PORT = 12346;

// Kolejka oczekujących połączeń 
static const int BACKLOG = 10;

// Mapa: sessionCode -> socket czekającego klienta (dla WIDEO).
std::map<std::string, int> waitingVideo;
// Mapa: sessionCode -> socket czekającego klienta (dla AUDIO).
std::map<std::string, int> waitingAudio;

// Struktura pomocnicza do odczytu "w ciagu" N bajtów
ssize_t readAll(int sock, char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(sock, buf + total, len - total, 0);
        if (r <= 0) {
            return r; // błąd lub rozłączenie
        }
        total += r;
    }
    return total;
}

// Funkcja obsługująca DWÓCH klientów w procesie potomnym
void handleTwoClients(int sockA, int sockB, const char* desc) {
    std::cout << "[Child " << getpid() << "] Obsługa pary klientów (" << desc << ")..." << std::endl;

    fd_set readfds;
    int maxfd = (sockA > sockB ? sockA : sockB) + 1;

    char buffer[4096];

    while (true) {
        FD_ZERO(&readfds);
        FD_SET(sockA, &readfds);
        FD_SET(sockB, &readfds);

        int ret = select(maxfd, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            std::cerr << "[Child " << getpid() << "][" << desc << "] Błąd select().\n";
            break;
        }

        // Sprawdzamy sockA
        if (FD_ISSET(sockA, &readfds)) {
            ssize_t recvd = recv(sockA, buffer, sizeof(buffer), 0);
            if (recvd <= 0) {
                // Klient A się rozłączył
                std::cerr << "[Child " << getpid() << "][" << desc << "] Klient A rozłączony.\n";
                break;
            }
            // Przesyłamy do B
            send(sockB, buffer, recvd, 0);
        }

        // Sprawdzamy sockB
        if (FD_ISSET(sockB, &readfds)) {
            ssize_t recvd = recv(sockB, buffer, sizeof(buffer), 0);
            if (recvd <= 0) {
                // Klient B się rozłączył
                std::cerr << "[Child " << getpid() << "][" << desc << "] Klient B rozłączony.\n";
                break;
            }
            // Przesyłamy do A
            send(sockA, buffer, recvd, 0);
        }
    }

    // Koniec obsługi - zamykamy obie strony
    close(sockA);
    close(sockB);

    // Kończymy proces potomny
    _exit(0);
}

// Do unikania zombie-procesów
void sigchld_handler(int) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// Funkcja pomocnicza do tworzenia gniazda, bindowania i słuchania.

int createListeningSocket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    if (listen(sock, BACKLOG) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int main() {
    signal(SIGCHLD, sigchld_handler);

    int videoSock = createListeningSocket(VIDEO_PORT);
    int audioSock = createListeningSocket(AUDIO_PORT);

    if (videoSock < 0) {
        std::cerr << "[Serwer] Błąd przy starcie gniazda wideo.\n";
        return 1;
    }
    if (audioSock < 0) {
        std::cerr << "[Serwer] Błąd przy starcie gniazda audio.\n";
        return 1;
    }

    std::cout << "[Serwer] Nasłuchiwanie:\n"
              << "  - WIDEO na porcie " << VIDEO_PORT << "\n"
              << "  - AUDIO na porcie " << AUDIO_PORT << "\n";

    // Główna pętla: obsługujemy jednocześnie 2 gniazda nasłuchujące (videoSock + audioSock)
    while (true) {


        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(videoSock, &readfds);
        FD_SET(audioSock, &readfds);

        int maxfd = (videoSock > audioSock ? videoSock : audioSock) + 1;
        
        int ret = select(maxfd, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                // select zostało przerwane sygnałem (SIGCHLD)
                // ignorujemy i kontynuujemy pętlę
                continue;
            } else {
                std::cerr << "[Serwer] Błąd select(): " << strerror(errno) << std::endl;
                break;
            }
        }


        // Sprawdzamy, czy mamy nowe połączenie WIDEO
        if (FD_ISSET(videoSock, &readfds)) {
            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int clientSock = accept(videoSock, (sockaddr*)&clientAddr, &addrLen);
            if (clientSock >= 0) {
                std::cout << "[Serwer] WIDEO: Nowe połączenie od "
                          << inet_ntoa(clientAddr.sin_addr) << ":"
                          << ntohs(clientAddr.sin_port) << std::endl;

                // Odbieramy sessionCode
                int length = 0;
                if (readAll(clientSock, (char*)&length, sizeof(length)) <= 0) {
                    std::cerr << "[Serwer-WIDEO] Błąd odczytu długości sessionCode.\n";
                    close(clientSock);
                    continue;
                }

                if (length <= 0 || length > 1024) {
                    std::cerr << "[Serwer-WIDEO] Błędny rozmiar sessionCode.\n";
                    close(clientSock);
                    continue;
                }

                std::vector<char> buf(length);
                if (readAll(clientSock, buf.data(), length) <= 0) {
                    std::cerr << "[Serwer-WIDEO] Błąd odczytu sessionCode.\n";
                    close(clientSock);
                    continue;
                }
                std::string sessionCode(buf.begin(), buf.end());
                std::cout << "[Serwer-WIDEO] sessionCode='" << sessionCode << "'\n";

                // Parowanie
                auto it = waitingVideo.find(sessionCode);
                if (it == waitingVideo.end()) {
                    // Nie ma jeszcze klienta wideo z tym kodem
                    waitingVideo[sessionCode] = clientSock;
                    std::cout << "[Serwer-WIDEO] Pierwszy klient w sesji '" << sessionCode << "'.\n";
                } else {
                    // Mamy już czekającego
                    int sockA = it->second;
                    int sockB = clientSock;
                    waitingVideo.erase(it);

                    std::cout << "[Serwer-WIDEO] Mamy parę w sesji '" << sessionCode << "'. fork()...\n";
                    pid_t pid = fork();
                    if (pid < 0) {
                        std::cerr << "[Serwer-WIDEO] Błąd fork().\n";
                        close(sockA);
                        close(sockB);
                        continue;
                    }
                    if (pid == 0) {
                        // Potomek
                        close(videoSock);  
                        close(audioSock);
                        handleTwoClients(sockA, sockB, "VIDEO");

                    } else {
                        // Rodzic
                        close(sockA);
                        close(sockB);
                    }
                }
            }
        }

        // Sprawdzamy, czy mamy nowe połączenie AUDIO
        if (FD_ISSET(audioSock, &readfds)) {
            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int clientSock = accept(audioSock, (sockaddr*)&clientAddr, &addrLen);
            if (clientSock >= 0) {
                std::cout << "[Serwer] AUDIO: Nowe połączenie od "
                          << inet_ntoa(clientAddr.sin_addr) << ":"
                          << ntohs(clientAddr.sin_port) << std::endl;

                // Odbieramy sessionCode
                int length = 0;
                if (readAll(clientSock, (char*)&length, sizeof(length)) <= 0) {
                    std::cerr << "[Serwer-AUDIO] Błąd odczytu długości sessionCode.\n";
                    close(clientSock);
                    continue;
                }

                if (length <= 0 || length > 1024) {
                    std::cerr << "[Serwer-AUDIO] Błędny rozmiar sessionCode.\n";
                    close(clientSock);
                    continue;
                }

                std::vector<char> buf(length);
                if (readAll(clientSock, buf.data(), length) <= 0) {
                    std::cerr << "[Serwer-AUDIO] Błąd odczytu sessionCode.\n";
                    close(clientSock);
                    continue;
                }
                std::string sessionCode(buf.begin(), buf.end());
                std::cout << "[Serwer-AUDIO] sessionCode='" << sessionCode << "'\n";

                // Parowanie
                auto it = waitingAudio.find(sessionCode);
                if (it == waitingAudio.end()) {
                    waitingAudio[sessionCode] = clientSock;
                    std::cout << "[Serwer-AUDIO] Pierwszy klient w sesji '" << sessionCode << "'.\n";
                } else {
                    // Mamy już czekającego
                    int sockA = it->second;
                    int sockB = clientSock;
                    waitingAudio.erase(it);

                    std::cout << "[Serwer-AUDIO] Mamy parę w sesji '" << sessionCode << "'. fork()...\n";
                    pid_t pid = fork();
                    if (pid < 0) {
                        std::cerr << "[Serwer-AUDIO] Błąd fork().\n";
                        close(sockA);
                        close(sockB);
                        continue;
                    }
                    if (pid == 0) {
                        // Potomek
                        close(videoSock); 
                        close(audioSock);
                        handleTwoClients(sockA, sockB, "AUDIO");
                    } else {
                        // Rodzic
                        close(sockA);
                        close(sockB);
                    }
                }
            }
        }
    }

    // Zamykanie
    close(videoSock);
    close(audioSock);

    return 0;
}
