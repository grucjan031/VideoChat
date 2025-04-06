# VideoChat - Projekt Sieci Komputerowe2
## Temat zadania
Stworzenie serwera i klienta do wymiany audio i wideo między użytkownikami(Aplikacja typu skype).
## Opis protkołu komunikacyjnego
Protokół TCP, jest to protokół połączeniowy, co oznacza, że przed rozpoczęciem wymiany danych pomiędzy dwoma urządzeniami musi zostać nawiązane połączenie (tzw. handshake). TCP zapewnia nas, że wszystkie dane zostaną dostarczone - jeśli jakieś pakiety zaginą zostaną przesłane ponownie.
## Opis implementacji
Zastosowany został współbieżny serwer TCP w języku c++ z użyciem procesów potomnych. Serwer nasłuchuje wideo na porcie 12345 i audio na porcie 12346. Klient zgłasza się z wybranym kodem sesji do serwera a ten następnie jeżeli nie ma drugiego klienta z takim samym kodem sesji zapisuje jego socket i gdy zgłosi się klient z takim samym kodem sesji tworzony jest proces potomny do obsługi tych dwóch klientów.
## Opis zawartości plików źródłowych
**server.cpp**
- readAll(int sock, char* buf, size_t len) - Odczytuje określoną liczbę bajtów (len) z gniazda (sock) i zapisuje je do bufora (buf).
- handleTwoClients(int sockA, int sockB, const char* desc) - Obsługuje dwukierunkową komunikację między dwoma klientami.
- sigchld_handler(int) - Obsługuje sygnał SIGCHLD, aby uniknąć zombie-procesów.
- createListeningSocket(int port) - Tworzy gniazdo TCP, które nasłuchuje na podanym porcie.
- main() - Monitoruje aktywność na obu gniazdach nasłuchujących za pomocą select(). Obsługuje przychodzące połączenia dla wideo i audio.


**main.py**
- start_connection(self) - Nawiązuje połączenie z serwerem dla wideo i audio.
- stop_connection(self) - Zatrzymuje wszystkie aktywne połączenia (wideo i audio).
- on_closing(self) - Wywoływana, gdy użytkownik zamyka okno aplikacji, zatrzymuje połączenie i zamyka aplikację.
- receive_frames(self) - Odbiera strumień wideo z serwera.
- send_frames(self) - Wysyła strumień wideo (obraz z kamery) do serwera.
- cv2_to_tk(frame) - Konwertuje obraz z formatu OpenCV na format zgodny z Tkinter.
- send_audio(self) - Wysyła dane audio z mikrofonu do serwera.
- receive_audio(self) - Odbiera dane audio z serwera.
## Sposób kompilacji, uruchomienia i obsługi projektów
Kompilacja i uruchomienie serwera g++ -Wall-o server.o server.cpp Następnie ./server.o
Uruchomienie klienta python main.python
Następnie wypełniamy wszystkie pola na górze panelu i klikamy połącz