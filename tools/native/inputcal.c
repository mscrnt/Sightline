/*
 * inputcal - find out what the hardware actually sends.
 *
 * Build:  tools/native/inputcal.sh
 * Run:    build/native/inputcal            guided calibration
 *         build/native/inputcal --monitor  print everything, continuously
 *
 * Why this exists. The native port synthesises an N64 pad, and every mapping
 * in it was decided by reading the decomp and reasoning about control styles.
 * That produced input that "kind of" works: forward on the wrong key, look and
 * move on the same axis, left and right reversed. Each of those is a guess
 * that survived because nothing measured it.
 *
 * This measures it. It opens no window and reads no ROM - SDL's gamepad layer
 * needs neither - so it can run in a terminal beside the game.
 *
 * It deliberately reports RAW SDL state and nothing else. It does not know
 * what the game expects, does not consult a control style, and does not try to
 * be clever: the whole point is a ground truth to check the mapping against.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define AXIS_ON  12000     /* of +-32767: past this an axis counts as pressed */
#define TRIG_ON   8000

static SDL_GameController *pad;
static SDL_Joystick       *joy;

static const char *btn_name(int b)
{
    const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton) b);
    return n ? n : "?";
}
static const char *axis_name(int a)
{
    const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis) a);
    return n ? n : "?";
}

/* Describe every input currently active. Returns how many were found. */
static int describe_active(char *out, size_t cap)
{
    int i, found = 0;
    size_t n = 0;

    out[0] = '\0';
    for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton) i)) {
            n += (size_t) snprintf(out + n, cap - n, "%sbutton:%s",
                                   found ? " + " : "", btn_name(i));
            found++;
        }
    }
    for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
        int v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis) i);
        int on = (i == SDL_CONTROLLER_AXIS_TRIGGERLEFT
                  || i == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                 ? (v > TRIG_ON) : (v > AXIS_ON || v < -AXIS_ON);
        if (on) {
            n += (size_t) snprintf(out + n, cap - n, "%saxis:%s=%+d",
                                   found ? " + " : "", axis_name(i), v);
            found++;
        }
    }
    return found;
}

static void drain(void)
{
    SDL_Event e;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e)) { /* discard */ }
}

/* Wait for the user to press something, then for them to let go. */
static int capture(const char *prompt, char *out, size_t cap)
{
    int quiet = 0;

    printf("\n  %-28s ", prompt);
    fflush(stdout);

    /* Require a clear release first, so a held-over input is not recaptured. */
    for (;;) {
        char tmp[512];
        drain();
        if (describe_active(tmp, sizeof tmp) == 0) {
            if (++quiet > 10) break;
        } else {
            quiet = 0;
        }
        SDL_Delay(16);
    }
    for (;;) {
        drain();
        if (describe_active(out, cap) > 0) break;
        SDL_Delay(16);
    }
    /* Let it settle so a stick's peak is caught rather than its first crossing. */
    SDL_Delay(180);
    drain();
    describe_active(out, cap);
    printf("%s\n", out);
    fflush(stdout);
    return 1;
}

int main(int argc, char **argv)
{
    int monitor = (argc > 1 && strcmp(argv[1], "--monitor") == 0);
    int i, n;

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) { joy = SDL_GameControllerGetJoystick(pad); break; }
        }
    }
    if (pad == NULL) {
        printf("No gamepad found (%d joystick(s) seen).\n", n);
        printf("If your pad is connected, SDL may not have a mapping for it.\n");
        SDL_Quit();
        return 1;
    }
    printf("pad: %s\n", SDL_GameControllerName(pad));

    if (monitor) {
        char cur[512], last[512];
        last[0] = '\0';
        printf("Monitoring. Press things; Ctrl-C to stop.\n");
        for (;;) {
            drain();
            describe_active(cur, sizeof cur);
            if (strcmp(cur, last) != 0) {
                printf("  %s\n", cur[0] ? cur : "(neutral)");
                fflush(stdout);
                strcpy(last, cur);
            }
            SDL_Delay(16);
        }
    }

    printf("\nPress each control as asked. Hold it until it prints, then let go.\n");
    printf("Press whatever YOU want that action to be - this records your\n");
    printf("preference, it is not a quiz with a right answer.\n");

    {
        static const char *asks[] = {
            "MOVE FORWARD",
            "MOVE BACKWARD",
            "STRAFE LEFT",
            "STRAFE RIGHT",
            "LOOK LEFT",
            "LOOK RIGHT",
            "LOOK UP",
            "LOOK DOWN",
            "FIRE",
            "AIM",
            "NEXT WEAPON",
            "RELOAD / ACTION",
            "PAUSE (watch)",
            NULL
        };
        char got[512];
        printf("\n--- results ---\n");
        for (i = 0; asks[i] != NULL; i++) {
            capture(asks[i], got, sizeof got);
        }
    }

    printf("\nDone. Paste this whole output back.\n");
    SDL_GameControllerClose(pad);
    SDL_Quit();
    return 0;
}
