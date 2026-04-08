#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <poll.h>

#define DEBOUNCE          50000
#define DOT_TIME         150000
#define DASH_TIME        (2 * DOT_TIME)
#define SYMBOL_SPACE     DOT_TIME
#define LETTER_SPACE     (3 * DOT_TIME)
#define WORD_SPACE       (5 * DOT_TIME)

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

enum AppMode {
  SEND,
  LISTEN,
};

volatile enum AppMode mode = SEND;
volatile int running = 1;

void sigint_handler(int sig) {
    if (mode == SEND) {
        mode = LISTEN;
        printf("\n[TRYB ODBIORU]");
    } else {
        mode = SEND;
        printf("\n[TRYB NADAWANIA]");
    }
}
void sigquit_handler(int sig) { running = 0; }

void print_morse_table(void) {
    printf("======================================\n");
    for (int i = 0; i < 18; i++) {
        printf(" %c   | %-15s ", morse_chars[i], morse_codes[i]);
        int j = i + 18;
        if (j < 36) {
            printf(" %c   | %-15s", morse_chars[j], morse_codes[j]);
        }
        printf("\n");
    }
    printf("======================================\n");
}

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
            morse_delay(WORD_SPACE);
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

void print_usage(const char *prog) {
    printf("Użycie: %s [led_chip] [led_line] [button_line]\n", prog);
    printf("Przykład:  %s gpiochip0 27 18\n", prog);
}

void decode_morse_input(unsigned int button_line_num, const char *chipname)
{
    struct timespec press_time, release_time, start_wait, now;
    struct gpiod_line_event ev;          
    char current_symbol[16] = "";

    printf("\n=== TRYB ODBIORU MORSE'A (Ctrl+C aby wrócić) ===\n");
    printf("Naciskaj przycisk na linii %u chipu %s\n", button_line_num, chipname);

    while (running && mode == LISTEN) {
        if (gpiod_line_event_wait(button_line, NULL) <= 0)
            continue;

        if (gpiod_line_event_read(button_line, &ev) < 0)
            continue;

        clock_gettime(CLOCK_MONOTONIC, &press_time);

        while (gpiod_line_get_value(button_line) == 0 && running) {
            usleep(5000);               
        }
        clock_gettime(CLOCK_MONOTONIC, &release_time);

        long duration_us = (release_time.tv_sec - press_time.tv_sec) * 1000000LL +
                           (release_time.tv_nsec - press_time.tv_nsec) / 1000;

        if (duration_us < DEBOUNCE) continue; 

        if (duration_us < DASH_TIME) {
            strcat(current_symbol, ".");
            printf(".");
        } else {
            strcat(current_symbol, "-");
            printf("-");
        }
        fflush(stdout);

        clock_gettime(CLOCK_MONOTONIC, &start_wait);

        while (running && mode == LISTEN) {
            if (gpiod_line_get_value(button_line) == 0) break;

            clock_gettime(CLOCK_MONOTONIC, &now);
            long gap = (now.tv_sec - start_wait.tv_sec) * 1000000LL +
                       (now.tv_nsec - start_wait.tv_nsec) / 1000;

            if (gap > LETTER_SPACE) { 
                int found = 0;
                for (int i = 0; i < 36; i++) {
                    if (strcmp(current_symbol, morse_codes[i]) == 0) {
                        printf(" → %c\n", morse_chars[i]);
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    printf(" ? (nieznany kod)\n");

                current_symbol[0] = '\0';

                break;
            }

            usleep(DOT_TIME / 10);
        }
    }
}

// ==================== MAIN ====================
int main(int argc, char **argv) {
    const char *chipname = "gpiochip0";
    unsigned int led_line_num = 27;
    unsigned int button_line_num = 18;

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
	    return 0;
        }
        chipname = argv[1];
    }
    if (argc >= 3) led_line_num = (unsigned int)strtoul(argv[2], NULL, 10);
    if (argc >= 4) button_line_num = (unsigned int)strtoul(argv[3], NULL, 10);

    signal(SIGINT,  sigint_handler);   
    signal(SIGQUIT, sigquit_handler); 

    chip = gpiod_chip_open_by_name(chipname);
    if (!chip) { 
        fprintf(stderr, "Nie można otworzyć %s\n", chipname); 
        return 1; 
    }

    led_line = gpiod_chip_get_line(chip, led_line_num);
    if (gpiod_line_request_output(led_line, "morse_led", 0) < 0) {
        fprintf(stderr, "Nie można skonfigurować linii LED\n");
        goto cleanup;
    }

    button_line = gpiod_chip_get_line(chip, button_line_num);
    if (!button_line) {
        fprintf(stderr, "Nie można pobrać linii przycisku\n");
        goto cleanup;
    }
    gpiod_line_request_both_edges_events(button_line, "morse_button");
    
    printf("Program uruchomiony -> chip: %s, LED: %u, BTN: %u\n",
           chipname, led_line_num, button_line_num);

    printf("\n=== STEROWANIE ===\n");
    printf("Tryb nadawania: pisz tekst i naciśnij Enter\n");
    printf("Ctrl+C          przełączanie pomiędzy trybami odbioru/nadawania\n");
    printf("Ctrl+\\          wyjśćie z aplikacji\n\n");

    print_morse_table();
  
    char line[256];

    while (running) {
        if (mode == LISTEN) {
            decode_morse_input(button_line_num, chipname);
            mode = SEND;
            printf("\nPowrót do trybu nadawania.\n\n");
            continue;
        }

        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };

        if (poll(&pfd, 1, 100) <= 0)  
            continue;

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (errno == EINTR) continue;  
            break;                         
        }

        line[strcspn(line, "\n")] = 0;

        if (strlen(line) == 0) continue;

        printf("Nadaję: \"%s\"\n", line);
        send_morse(led_line, line);
    }

cleanup:
    if (led_line)    gpiod_line_release(led_line);
    if (button_line) gpiod_line_release(button_line);
    if (chip)        gpiod_chip_close(chip);

    printf("\nKoniec programu.\n");
    return 0;
}
