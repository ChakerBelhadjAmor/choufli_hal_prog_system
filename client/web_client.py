"""
web_client.py — Flask web interface for Choufli Hal Clinic
عيادة شوفلي حل — نظام المواعيد الإلكتروني
"""

from flask import Flask, request, jsonify, send_from_directory
import socket
import os

# ── Configuration ───────────────────────────────────────────────────────────
C_SERVER_IP   = "127.0.0.1"
C_SERVER_PORT = 8080
RECV_SIZE     = 4096

app = Flask(__name__, static_folder="static")

# ── Helpers ──────────────────────────────────────────────────────────────────

def extraire(data: str) -> str:
    """Strip 'TYPE|' prefix and return content."""
    idx = data.find("|")
    return data[idx + 1:] if idx != -1 else data


def recv_message(sock: socket.socket) -> str:
    """Receive a full message from the C server."""
    data = sock.recv(RECV_SIZE)
    return data.decode("utf-8", errors="replace")


# ── Routes ───────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return send_from_directory(os.path.dirname(__file__), "index.html")


@app.route("/static/<path:filename>")
def static_files(filename):
    return send_from_directory(os.path.dirname(__file__), filename)


@app.route("/consulter", methods=["POST"])
def consulter():
    nom      = request.form.get("nom", "").strip()
    symptomes = request.form.get("symptomes", "").strip()

    if not nom or not symptomes:
        return jsonify({"success": False, "error": "الاسم والأعراض مطلوبان"}), 400

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(300)  # 5-minute timeout — patient may wait in queue
        sock.connect((C_SERVER_IP, C_SERVER_PORT))

        # 1. Receive BONJOUR (discard)
        recv_message(sock)

        # 2. Send name
        sock.sendall((nom + "\n").encode("utf-8"))

        # 3. Receive ATTENTE
        raw_attente = recv_message(sock)
        msg_attente = extraire(raw_attente)

        # 4. Receive VOTRE_TOUR (BLOCKING — patient waits in queue)
        raw_tour = recv_message(sock)
        msg_tour = extraire(raw_tour)

        # 5. Send symptoms
        sock.sendall((symptomes + "\n").encode("utf-8"))

        # 6. Receive DIAGNOSTIC
        raw_diag  = recv_message(sock)
        diagnostic = extraire(raw_diag)

        return jsonify({
            "success":     True,
            "msg_attente": msg_attente,
            "msg_tour":    msg_tour,
            "diagnostic":  diagnostic,
        })

    except socket.timeout:
        return jsonify({"success": False,
                        "error": "انتهى وقت الانتظار — جرب مرة أخرى لاحقاً"}), 504
    except ConnectionRefusedError:
        return jsonify({"success": False,
                        "error": "الخادم غير متاح — تأكد أن الدكتور سليمان في العيادة!"}), 503
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500
    finally:
        sock.close()


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("🏥 شوفلي حل Web Client — http://localhost:5000")
    print(f"   Connecting to C server at {C_SERVER_IP}:{C_SERVER_PORT}")
    app.run(host="0.0.0.0", port=5000, threaded=True, debug=False)
