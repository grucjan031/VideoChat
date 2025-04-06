import tkinter as tk
from tkinter import ttk
import cv2
import socket
import struct
import pickle
import threading
from PIL import Image, ImageTk
import pyaudio

class VideoChatClient(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("VideoChatClient")
        self.geometry("1400x600")
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

        self.session_code = tk.StringVar(value="ABC123")

        self.server_ip = tk.StringVar(value="127.0.0.1")
        self.video_port = 12345     
        self.audio_port = 12346                       
        self.is_running = False

        self.socket_client = None
        self.audio_socket = None  
        self.capture = None


        self.p_audio = None
        self.stream_in = None
        self.stream_out = None

        # Parametry audio
        self.CHUNK = 1024
        self.FORMAT = pyaudio.paInt16
        self.CHANNELS = 1
        self.RATE = 44100

        # Górny panel GUI
        connection_frame = ttk.Frame(self)
        connection_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)

        ttk.Label(connection_frame, text="Server IP:").pack(side=tk.LEFT)
        ttk.Entry(connection_frame, textvariable=self.server_ip, width=15).pack(side=tk.LEFT, padx=5)


        ttk.Label(connection_frame, text="Session Code:").pack(side=tk.LEFT)
        ttk.Entry(connection_frame, textvariable=self.session_code, width=10).pack(side=tk.LEFT, padx=5)

        self.btn_connect = ttk.Button(connection_frame, text="Połącz", command=self.start_connection)
        self.btn_connect.pack(side=tk.LEFT, padx=5)

        self.btn_disconnect = ttk.Button(connection_frame, text="Rozłącz", command=self.stop_connection, state=tk.DISABLED)
        self.btn_disconnect.pack(side=tk.LEFT, padx=5)

        # Panele wideo
        video_frame = ttk.Frame(self)
        video_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self.local_label = ttk.Label(video_frame, text="Nasza kamerka")
        self.local_label.pack(side=tk.LEFT, expand=True, fill=tk.BOTH, padx=5, pady=5)

        self.remote_label = ttk.Label(video_frame, text="Kamerka rozmówcy")
        self.remote_label.pack(side=tk.LEFT, expand=True, fill=tk.BOTH, padx=5, pady=5)

        self.update()

    def start_connection(self):
        # Nawiązuje połączenie z serwerem i uruchamia wątki
        if self.is_running:
            return

        ip = self.server_ip.get()
        code = self.session_code.get()

        try:
         
            self.socket_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket_client.connect((ip, self.video_port))
            self.is_running = True
            print(f"[Klient] Połączono z serwerem video {ip}:{self.video_port}")

            # Wysyłamy kod sesji do serwera
            code_bytes = code.encode('utf-8')
            length = len(code_bytes)
            # Najpierw 4 bajty (int), potem sam kod
            self.socket_client.sendall(struct.pack("i", length) + code_bytes)

            self.audio_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.audio_socket.connect((ip, self.audio_port))
            print(f"[Klient] Połączono z serwerem audio {ip}:{self.audio_port}")
            
            self.audio_socket.sendall(struct.pack("i", length) + code_bytes)

            self.btn_connect.config(state=tk.DISABLED)
            self.btn_disconnect.config(state=tk.NORMAL)

            # Wątek odbioru wideo
            self.receive_thread = threading.Thread(target=self.receive_frames, daemon=True)
            self.receive_thread.start()

            # Wątek wysyłania wideo
            self.capture = cv2.VideoCapture(0)
            if not self.capture.isOpened():
                print("Nie udało się otworzyć kamerki!")
                self.is_running = False
                return

            self.send_thread = threading.Thread(target=self.send_frames, daemon=True)
            self.send_thread.start()

            # Przygotowanie do audio
            self.p_audio = pyaudio.PyAudio()
            # Strumień wejściowy (mikrofon)
            self.stream_in = self.p_audio.open(format=self.FORMAT,
                                               channels=self.CHANNELS,
                                               rate=self.RATE,
                                               input=True,
                                               frames_per_buffer=self.CHUNK)
            # Strumień wyjściowy (głośniki/słuchawki)
            self.stream_out = self.p_audio.open(format=self.FORMAT,
                                                channels=self.CHANNELS,
                                                rate=self.RATE,
                                                output=True,
                                                frames_per_buffer=self.CHUNK)

            # Wątki audio
            self.send_audio_thread = threading.Thread(target=self.send_audio, daemon=True)
            self.send_audio_thread.start()

            self.receive_audio_thread = threading.Thread(target=self.receive_audio, daemon=True)
            self.receive_audio_thread.start()

        except Exception as e:
            print("[Błąd] Nie udało się nawiązać połączenia:", e)
            self.is_running = False
            if self.socket_client:
                self.socket_client.close()
            if self.audio_socket:
                self.audio_socket.close()

    def stop_connection(self):
        # Zamyka połączenie i wyłącza wysyłanie/odbieranie.
        self.is_running = False
        self.btn_connect.config(state=tk.NORMAL)
        self.btn_disconnect.config(state=tk.DISABLED)

        # Zamykanie socketów
        for s in [self.socket_client, self.audio_socket]:
            if s:
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except:
                    pass
                s.close()

        self.socket_client = None
        self.audio_socket = None

        # Zamykanie kamerki
        if self.capture:
            self.capture.release()
            self.capture = None

        # Zamykanie strumieni audio
        if self.stream_in:
            self.stream_in.stop_stream()
            self.stream_in.close()
            self.stream_in = None

        if self.stream_out:
            self.stream_out.stop_stream()
            self.stream_out.close()
            self.stream_out = None

        if self.p_audio:
            self.p_audio.terminate()
            self.p_audio = None

        self.local_label.config(text="Nasza kamerka", image="", compound=tk.NONE)
        self.remote_label.config(text="Kamerka rozmówcy", image="", compound=tk.NONE)

    def on_closing(self):
        self.stop_connection()
        self.destroy()

    def receive_frames(self):
        # Odbiera wideo z serwera i wyświetla w remote_label.
        data = b""
        payload_size = struct.calcsize("I")

        while self.is_running and self.socket_client:
            try:
                while len(data) < payload_size:
                    packet = self.socket_client.recv(4096)
                    if not packet:
                        return
                    data += packet

                packed_msg_size = data[:payload_size]
                data = data[payload_size:]
                msg_size = struct.unpack("I", packed_msg_size)[0]

                while len(data) < msg_size:
                    data += self.socket_client.recv(4096)

                frame_data = data[:msg_size]
                data = data[msg_size:]

                frame = pickle.loads(frame_data)
                frame = cv2.imdecode(frame, cv2.IMREAD_COLOR)

                if frame is not None:
                    img_tk = self.cv2_to_tk(frame)
                    self.remote_label.config(image=img_tk, compound=tk.NONE, text="")
                    self.remote_label.image = img_tk

            except Exception as e:
                print("[Klient] Błąd w receive_frames:", e)
                break

    def send_frames(self):
        # Wysyła obraz z kamerki do serwera (wideo).
        while self.is_running and self.capture is not None and self.socket_client:
            ret, frame = self.capture.read()
            if not ret:
                continue

            # Lokalne wyświetlanie (opcjonalne)
            img_tk = self.cv2_to_tk(frame)
            self.local_label.config(image=img_tk, compound=tk.NONE, text="")
            self.local_label.image = img_tk

            # Kompresja do JPEG
            encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 80]
            result, encoded_img = cv2.imencode('.jpg', frame, encode_param)
            if not result:
                continue

            data = pickle.dumps(encoded_img, 0)
            size = len(data)

            try:
                self.socket_client.sendall(struct.pack("I", size) + data)
            except Exception as e:
                print("[Klient] Błąd w send_frames:", e)
                break

            cv2.waitKey(10)

    def send_audio(self):
        # Wysyła dane audio z mikrofonu do serwera.
        while self.is_running and self.audio_socket and self.stream_in:
            try:
                # Odczyt fragmentu z mikrofonu
                data = self.stream_in.read(self.CHUNK)
                # Wysłanie do serwera
                self.audio_socket.sendall(data)
            except Exception as e:
                print("[Klient] Błąd w send_audio:", e)
                break

    def receive_audio(self):
        # Odbiera dane audio z serwera i odtwarza je na głośnikach.
        while self.is_running and self.audio_socket and self.stream_out:
            try:
                data = self.audio_socket.recv(self.CHUNK)
                if not data:
                    break
                self.stream_out.write(data)
            except Exception as e:
                print("[Klient] Błąd w receive_audio:", e)
                break

    @staticmethod
    def cv2_to_tk(frame):
        # Konwersja obrazu z OpenCV (BGR) na format zgodny z Tkinter.
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        pil_image = Image.fromarray(frame_rgb)
        return ImageTk.PhotoImage(image=pil_image)

def main():
    app = VideoChatClient()
    app.mainloop()

if __name__ == "__main__":
    main()
