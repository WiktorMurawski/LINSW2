#!/usr/bin/env python3
"""
Morse LED + Decoder
Użycie: python3 morse.py [chipname] [led_line] [button_line]
Przykład: python3 morse.py gpiochip0 30 20

Ctrl+C  – przełącz między trybem nadawania i odbioru
Ctrl+\\  – zakończ program
"""

import sys
import signal
import time
import select
import gpiod

# ==================== STAŁE CZASOWE (sekundy) ====================
DEBOUNCE      = 0.05
DOT_TIME      = 0.15
DASH_TIME     = DOT_TIME * 2
SYMBOL_SPACE  = DOT_TIME
LETTER_SPACE  = DOT_TIME * 3
WORD_SPACE    = DOT_TIME * 5

# ==================== TABLICA MORSE'A ====================
MORSE_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
MORSE_CODES = [
    ".-",   "-...", "-.-.", "-..",  ".",     # A-E
    "..-.", "--.",  "....", "..",   ".---",  # F-J
    "-.-",  ".-..", "--",   "-.",   "---",   # K-O
    ".--.", "--.-", ".-.",  "...",  "-",     # P-T
    "..-",  "...-", ".--",  "-..-", "-.--", "--..",  # U-Z
    "-----", ".----", "..---", "...--", "....-",  # 0-4
    ".....", "-....", "--...", "---..", "----.",   # 5-9
]

DECODE_MAP = {code: char for char, code in zip(MORSE_CHARS, MORSE_CODES)}

# ==================== STAN GLOBALNY ====================
mode    = "SEND"   # "SEND" | "LISTEN"
running = True

def sigint_handler(sig, frame):
    global mode
    if mode == "SEND":
        mode = "LISTEN"
        print("\n[→ TRYB ODBIORU]")
    else:
        mode = "SEND"
        print("\n[→ TRYB NADAWANIA]")

def sigquit_handler(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT,  sigint_handler)
signal.signal(signal.SIGQUIT, sigquit_handler)

# ==================== WYŚWIETLANIE TABLICY ====================
def print_morse_table():
    print("=" * 38)
    print(" CHAR |  KOD             CHAR |  KOD")
    print("=" * 38)
    for i in range(18):
        j = i + 18
        right = f"  {MORSE_CHARS[j]}   | {MORSE_CODES[j]:<15}" if j < 36 else ""
        print(f" {MORSE_CHARS[i]}   | {MORSE_CODES[i]:<15} {right}")
    print("=" * 38 + "\n")

# ==================== NADAWANIE ====================
def flash(led_line: gpiod.Line, code: str):
    for symbol in code:
        led_line.set_value(1)
        time.sleep(DOT_TIME if symbol == '.' else DASH_TIME)
        led_line.set_value(0)
        time.sleep(SYMBOL_SPACE)

def send_morse(led_line: gpiod.Line, text: str):
    for ch in text.upper():
        if ch == ' ':
            time.sleep(WORD_SPACE - LETTER_SPACE)
            continue

        if 'A' <= ch <= 'Z':
            idx = ord(ch) - ord('A')
        elif '0' <= ch <= '9':
            idx = ord(ch) - ord('0') + 26
        else:
            continue

        flash(led_line, MORSE_CODES[idx])
        time.sleep(LETTER_SPACE - SYMBOL_SPACE)

# ==================== ODBIÓR ====================
def decode_morse_input(button_line: gpiod.Line):
    global running, mode

    current_symbol = ""

    print("\n=== TRYB ODBIORU MORSE'A ===")
    print("Naciskaj przycisk, Ctrl+C aby wrócić do nadawania\n")

    while running and mode == "LISTEN":
        # Czekamy na zdarzenie (timeout 100 ms, żeby sprawdzać flagi)
        if not button_line.event_wait(sec=0, nsec=100_000_000):
            continue

        ev = button_line.event_read()
        if ev.type != gpiod.LineEvent.RISING_EDGE:
            continue

        # Rejestrujemy czas wciśnięcia
        press_time = time.monotonic()

        # Czekamy na puszczenie (falling edge / wartość 1 w active-low)
        while button_line.get_value() == 0 and running:
            time.sleep(0.005)

        release_time = time.monotonic()
        duration = release_time - press_time

        if duration < DEBOUNCE:
            continue

        # Kropka czy kreska?
        if duration < DASH_TIME:
            current_symbol += "."
            print(".", end="", flush=True)
        else:
            current_symbol += "-"
            print("-", end="", flush=True)

        # Czekamy na przerwę między symbolami
        start_wait = time.monotonic()

        while running and mode == "LISTEN":
            if button_line.get_value() == 0:
                # Przycisk znowu wciśnięty – nowy symbol
                break

            gap = time.monotonic() - start_wait

            if gap > LETTER_SPACE:
                char = DECODE_MAP.get(current_symbol)
                if char:
                    print(f" → {char}")
                else:
                    print(" ? (nieznany kod)")

                current_symbol = ""
                break

            time.sleep(DOT_TIME / 10)

# ==================== MAIN ====================
def main():
    chipname        = "gpiochip0"
    led_line_num    = 30
    button_line_num = 20

    args = sys.argv[1:]

    if args and args[0] in ("-h", "--help"):
        print(f"Użycie: {sys.argv[0]} [chipname] [led_line] [button_line]")
        print(f"Przykład: {sys.argv[0]} gpiochip0 30 20")
        return

    if len(args) >= 1: chipname        = args[0]
    if len(args) >= 2: led_line_num    = int(args[1])
    if len(args) >= 3: button_line_num = int(args[2])

    try:
        chip = gpiod.Chip(chipname)
    except OSError:
        print(f"Nie można otworzyć {chipname}", file=sys.stderr)
        sys.exit(1)

    led_line    = chip.get_line(led_line_num)
    button_line = chip.get_line(button_line_num)

    led_cfg = gpiod.LineRequest()
    led_cfg.consumer     = "morse_led"
    led_cfg.request_type = gpiod.LineRequest.DIRECTION_OUTPUT
    led_line.request(led_cfg, default_val=0)

    btn_cfg = gpiod.LineRequest()
    btn_cfg.consumer     = "morse_button"
    btn_cfg.request_type = gpiod.LineRequest.EVENT_BOTH_EDGES
    button_line.request(btn_cfg)

    print(f"Morse LED + Decoder uruchomiony → chip: {chipname}, "
          f"LED: {led_line_num}, przycisk: {button_line_num}")

    print_morse_table()

    print("=== STEROWANIE ===")
    print("Tryb nadawania : pisz tekst i naciśnij Enter")
    print("Ctrl+C         : przełącz na tryb odbioru / nadawania")
    print("Ctrl+\\         : zakończ program\n")

    global running, mode

    try:
        while running:
            if mode == "LISTEN":
                print("\n=== TRYB ODBIORU AKTYWNY (Ctrl+C aby wrócić) ===")
                decode_morse_input(button_line)
                mode = "SEND"
                print("\nPowrót do trybu nadawania.\n")
                continue

            # Nieblokujące czytanie stdin (100 ms timeout)
            ready, _, _ = select.select([sys.stdin], [], [], 0.1)
            if not ready:
                continue

            line = sys.stdin.readline()
            if not line:
                break

            text = line.rstrip("\n")
            if not text:
                continue

            print(f'Nadaję: "{text}"')
            send_morse(led_line, text)

    finally:
        led_line.release()
        button_line.release()
        chip.close()
        print("\nKoniec programu.")

if __name__ == "__main__":
    main()

