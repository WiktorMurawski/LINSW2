#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>

#define DEFAULT_DOT_TIME 200000
#define DOT_TIME         DEFAULT_DOT_TIME   // µs
#define DASH_TIME        (3 * DOT_TIME)
#define SYMBOL_SPACE     DOT_TIME
#define LETTER_SPACE     (3 * DOT_TIME)
#define WORD_SPACE       (7 * DOT_TIME)

const char *morse_codes[36] = {
    ".-", "-...", "-.-.", "-..", ".", // ABCDE 
    "..-.", "--.", "....", "..", ".---", // FGHIJ
    "-.-", ".-..", "--", "-.", "---", // KLMNO
    ".--.", "--.-", ".-.", "...", "-", // PQRST
    "..-", "...-", ".--", "-..-", "-.--", "--..", // UVWXYZ
    "-----", ".----", "..---", "...--", "....-", // 01234 
    ".....", "-....", "--...", "---..", "----.", // 56789
};

const char *morse_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

struct gpiod_chip *chip = NULL;
struct gpiod_line *led_line = NULL;
struct gpiod_line *button_line = NULL;

volatile int running = 1;

void signal_handler(int sig) { running = 0; }

// ==================== TABLICA MORSE'A ====================
void print_morse_table(void) {
    for (int i = 0; i < 18; i++) {
        printf(" %c   | %-15s ", morse_chars[i], morse_codes[i]);
        int j = i + 18;
        if (j < 36) {
            printf(" %c   | %-15s", morse_chars[j], morse_codes[j]);
        }
        printf("\n");
    }
    printf("======================================\n\n");
}

// ==================== FUNKCJE MORSE ====================
void morse_delay(int us) {
    usleep(us);
}

void flash(struct gpiod_line *line, const char *code) {
    for (const char *p = code; *p; p++) {
        gpiod_line_set_value(line, 1);

        if (*p == '.')      morse_delay(DOT_TIME);
        else if (*p == '-') morse_delay(DASH_TIME);

        gpiod_line_set_value(line, 0);
        morse_delay(SYMBOL_SPACE);
    }
}

void send_morse(struct gpiod_line *led_line, const char *text) {
    for (const char *p = text; *p; p++) {
        char c = toupper(*p);

        if (c == ' ') {
            morse_delay(WORD_SPACE - LETTER_SPACE);
            continue;
        }

        int idx = -1;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= '0' && c <= '9') idx = c - '0' + 26;

        if (idx >= 0 && idx < 36) {
            flash(led_line, morse_codes[idx]);
            morse_delay(LETTER_SPACE - SYMBOL_SPACE);
        }
    }
}

// ==================== HELP & USAGE ====================
void print_usage(const char *prog) {
    printf("Użycie: %s [led_chip] [led_line] [button_chip] [button_line]\n", prog);
    printf("Domyślnie: gpiochip0 17 (LED)   gpiochip0 27 (przycisk)\n");
    printf("Przykład:  %s gpiochip0 17 gpiochip0 27\n", prog);
}

// ==================== DEKODER MORSE'A Z PRZYCISKU ====================
void decode_morse_input(unsigned int button_line_num, const char *chipname)
{
    struct timespec press_time, release_time, start_wait, now;
    struct gpiod_line_event ev;          // ← NOWOŚĆ – musimy mieć tę strukturę
    char current_symbol[16] = "";
    int dot_time = DEFAULT_DOT_TIME;

    printf("\n=== TRYB ODBIORU MORSE'A ===\n");
    printf("Naciskaj przycisk na linii %u chipu %s\n", button_line_num, chipname);
    printf("Krótko = kropka (.), długo = kreska (-)\n");
    printf("Dłuższa przerwa = koniec litery, bardzo długa = spacja\n");
    printf("Ctrl+C aby wyjść z trybu odbioru.\n\n");

    while (running) {
        // Czekamy na zdarzenie (wciśnięcie lub puszczenie)
        if (gpiod_line_event_wait(button_line, NULL) <= 0)
            continue;

        // Wczytujemy zdarzenie (MUSI być &ev, nie NULL!)
        if (gpiod_line_event_read(button_line, &ev) < 0)
            continue;

        // Rejestrujemy moment wciśnięcia
        clock_gettime(CLOCK_MONOTONIC, &press_time);

        // Czekamy na puszczenie przycisku (aktywny niski = wartość 0)
        while (gpiod_line_get_value(button_line) == 0 && running) {
            usleep(5000);               // debounce + polling
        }
        clock_gettime(CLOCK_MONOTONIC, &release_time);

        long duration_us = (release_time.tv_sec - press_time.tv_sec) * 1000000LL +
                           (release_time.tv_nsec - press_time.tv_nsec) / 1000;

        if (duration_us < 30000) continue;   // zbyt krótki – debounce

        if (duration_us < dot_time * 2) {
            strcat(current_symbol, ".");
            printf(".");
        } else {
            strcat(current_symbol, "-");
            printf("-");
        }
        fflush(stdout);

        // Czekamy na przerwę między symbolami
        clock_gettime(CLOCK_MONOTONIC, &start_wait);

        while (running) {
            if (gpiod_line_get_value(button_line) == 0) break;   // nowe wciśnięcie

            clock_gettime(CLOCK_MONOTONIC, &now);
            long gap = (now.tv_sec - start_wait.tv_sec) * 1000000LL +
                       (now.tv_nsec - start_wait.tv_nsec) / 1000;

            if (gap > dot_time * 5) {          // koniec litery
                // Dekodujemy
                int found = 0;
                for (int i = 0; i < 36; i++) {
                    if (strcmp(current_symbol, morse_codes[i]) == 0) {
                        printf(" → %c\n", morse_chars[i]);
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    printf(" → ? (nieznany kod)\n");

                current_symbol[0] = '\0';      // reset bufora

                if (gap > dot_time * 12)
                    printf(" (spacja)\n");

                break;
            }
            usleep(20000);
        }

        // Prosta adaptacja tempa
        if (strlen(current_symbol) == 1)
            dot_time = (dot_time * 7 + duration_us) / 8;
    }
}

// ==================== MAIN ====================
int main(int argc, char **argv) {
    const char *chipname = "gpiochip0";
    unsigned int led_line_num = 30;
    unsigned int button_line_num = 20; 

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Użycie: %s [chipname] [led_line] [button_line]\n", argv[0]);
            printf("Przykład: %s gpiochip0 30 20\n", argv[0]);
            return 0;
        }
        chipname = argv[1];
    }
    if (argc >= 3) led_line_num = (unsigned int)strtoul(argv[2], NULL, 10);
    if (argc >= 4) button_line_num = (unsigned int)strtoul(argv[3], NULL, 10);

    signal(SIGINT, signal_handler);

    chip = gpiod_chip_open_by_name(chipname);
    if (!chip) { fprintf(stderr, "Nie można otworzyć %s\n", chipname); return 1; }

    // LED jako wyjście
    led_line = gpiod_chip_get_line(chip, led_line_num);
    gpiod_line_request_output(led_line, "morse_led", 0);

    // Przycisk jako wejście z eventami (both edges)
    button_line = gpiod_chip_get_line(chip, button_line_num);
    gpiod_line_request_both_edges_events(button_line, "morse_button");

    printf("Morse LED + Decoder uruchomiony → chip: %s, LED: %u, przycisk: %u\n",
           chipname, led_line_num, button_line_num);

    print_morse_table();

    printf("Wpisz tekst i Enter → nadawanie na LED\n");
    printf("Wpisz 'odbior' i Enter → przełącz na tryb odbioru przez przycisk\n");
    printf("Pusta linia lub Ctrl+C → wyjście\n\n");

    char line[256];
    while (running && fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;

        if (strlen(line) == 0) break;

        if (strcmp(line, "odbior") == 0 || strcmp(line, "odb") == 0) {
            decode_morse_input(button_line_num, chipname);
            printf("\nPowrót do trybu nadawania.\n");
            continue;
        }

        printf("Nadaję: \"%s\"\n", line);
        // Nadawanie (flash na led_line)
        for (const char *p = line; *p; p++) {
            char c = toupper(*p);
            if (c == ' ') { morse_delay(7 * DEFAULT_DOT_TIME); continue; }

            int idx = -1;
            if (c >= 'A' && c <= 'Z') idx = c - 'A';
            else if (c >= '0' && c <= '9') idx = c - '0' + 26;

            if (idx >= 0) {
                flash(led_line, morse_codes[idx]);
                morse_delay(3 * DEFAULT_DOT_TIME);
            }
        }
    }

    // Sprzątanie
    if (led_line) gpiod_line_release(led_line);
    if (button_line) gpiod_line_release(button_line);
    if (chip) gpiod_chip_close(chip);

    printf("\nKoniec.\n");
    return 0;
}
