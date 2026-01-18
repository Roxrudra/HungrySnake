#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>

#include <time.h>

#include <termkey.h>

#include "data.h"

#define GAME_ROWS 17
#define GAME_COLS 25

extern const unsigned char screen[SCREEN_LEN] ;
extern const unsigned char level_maps[LEVELS][LEVEL_MAP_LEN] ;
extern const unsigned char game_over[GAME_OVER_LEN] ;
extern const unsigned char level_clear[LEVEL_CLEAR_LEN] ;

static TermKey *tk ;

struct pos {
    uint8_t row ;
    uint8_t col ;
} ;

enum direction {
    DIR_LEFT,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN
} ;

enum term_color {
    COLOR_SNAKE=32,
    COLOR_HEAD=92,
    COLOR_FOOD=93,
    COLOR_OBSTACKLE=91,
    COLOR_TEXT=97
} ;

static struct {
    int level ;
    int score ;
    int best_score[LEVELS] ;
    int avail_num ;
    enum direction dir_new ;
    enum direction dir_old ;
    struct pos food ;
    struct pos head ;
    struct pos tail ;
    struct pos avail[GAME_ROWS][GAME_COLS] ;
    struct pos avail_pos[GAME_ROWS][GAME_COLS] ;
} game = {
    .level = 0,
    .score = 0,
    .best_score = {0},
} ;

struct timespec time_diff(struct timespec start, struct timespec stop)
{
    struct timespec diff ;

    bool carry = (stop.tv_nsec < start.tv_nsec) ;
    bool valid = (stop.tv_sec >= start.tv_sec+carry) ;

    diff.tv_nsec = valid * ( (carry*1000000000L + stop.tv_nsec) - start.tv_nsec ) ;
    diff.tv_sec = valid * ( stop.tv_sec - start.tv_sec - carry ) ;

    return diff ;
}

void init_terminal()
{
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0) ;
    fcntl(STDIN_FILENO, F_SETFL, flags|O_NONBLOCK) ;
    printf("\x1b[?1049h\x1b[?25l") ;
    fflush(stdout) ;

    tk = termkey_new(0, TERMKEY_FLAG_SPACESYMBOL) ;
    if (!tk) exit(EXIT_FAILURE) ;
}

void restore_terminal()
{
    printf("\x1b[?1049l\x1b[?25h") ;
    termkey_destroy(tk) ;
}

void handle_exit_signal(int sig)
{
    (void) sig ;
    exit(EXIT_SUCCESS) ;
}

static inline int grid2arr(struct pos p)
{
    return p.row*GAME_COLS + p.col ;
}

static inline struct pos arr2grid(int k)
{
    struct pos p ;
    p.row = k/GAME_COLS ;
    p.col = k%GAME_COLS ;
    return p ;
}

#define G2S_ROW(i) i+3
#define G2S_COL(j) 2*j+2

bool put_head()
{
    printf("\x1b[%d;%dH\x1b[%dm", G2S_ROW(game.head.row), G2S_COL(game.head.col), COLOR_SNAKE) ;
    if (game.dir_new == game.dir_old) {
        if (game.dir_new == DIR_LEFT || game.dir_new == DIR_RIGHT) {
            printf ("▆▆") ; 
        } else {
            printf ("█▌") ;
        }
    } else {
        if ((game.dir_old == DIR_DOWN && game.dir_new == DIR_RIGHT) || (game.dir_old == DIR_LEFT && game.dir_new == DIR_UP) ) {
            printf ("█𜷤") ;
        } else if ((game.dir_old == DIR_UP && game.dir_new == DIR_RIGHT) || (game.dir_old == DIR_LEFT && game.dir_new == DIR_DOWN) ) {
            printf ("▆▆") ;
        } else if ((game.dir_old == DIR_DOWN && game.dir_new == DIR_LEFT) || (game.dir_old == DIR_RIGHT && game.dir_new == DIR_UP) ) {
            printf ("█▌") ;
        } else if ((game.dir_old == DIR_UP && game.dir_new == DIR_LEFT) || (game.dir_old == DIR_RIGHT && game.dir_new == DIR_DOWN) ) {
            printf ("▆𜵈") ;
        }
    }
    
    struct pos new_head; 
    new_head.row = (GAME_ROWS + game.head.row) + (game.dir_new == DIR_DOWN) - (game.dir_new == DIR_UP) ;
    new_head.col = (GAME_COLS + game.head.col) + (game.dir_new == DIR_RIGHT) - (game.dir_new == DIR_LEFT) ;

    new_head.row = (new_head.row) % GAME_ROWS ;
    new_head.col = (new_head.col) % GAME_COLS ;

    int k = grid2arr(game.avail_pos[new_head.row][new_head.col]) ;
    if ( k >= game.avail_num ) {
        // game over;
        return false ;
    }

    struct pos pi = game.avail_pos[new_head.row][new_head.col] ;
    struct pos ph = game.avail_pos[game.head.row][game.head.col] ;
    struct pos nz = arr2grid(game.avail_num-1) ;
    struct pos znz = game.avail[nz.row][nz.col] ;

    game.avail[pi.row][pi.col] = znz ;
    game.avail[nz.row][nz.col] = game.tail ;
    game.avail[ph.row][ph.col] = new_head ;

    game.avail_pos[new_head.row][new_head.col] = nz ;
    game.avail_pos[znz.row][znz.col] = pi ;

    game.head = new_head ;
    game.avail_num-- ;

    printf("\x1b[%d;%dH\x1b[%dm", G2S_ROW(game.head.row), G2S_COL(game.head.col), COLOR_HEAD) ;
    if (game.dir_new == DIR_LEFT || game.dir_new == DIR_RIGHT) {
        printf ("▆▆") ; 
    } else {
        printf ("█▌") ;
    }

    game.dir_old = game.dir_new ;
    return true ;
}

void cut_tail()
{
    printf("\x1b[%d;%dH  ", G2S_ROW(game.tail.row), G2S_COL(game.tail.col)) ;
    struct pos nz = arr2grid(game.avail_num) ;
    struct pos pt = game.avail_pos[game.tail.row][game.tail.col] ;

    game.avail_pos[game.head.row][game.head.col] = pt ;
    game.avail_pos[game.tail.row][game.tail.col] = nz ;

    game.tail = game.avail[pt.row][pt.col] ;
    game.avail_num++ ;
}

bool generate_food()
{
    if (game.avail_num == 0) return false ; 

    struct pos avail_food = arr2grid(rand() % game.avail_num) ;
    game.food = game.avail[avail_food.row][avail_food.col] ;
    printf("\x1b[%d;%dH\x1b[%dm▆𜵈", G2S_ROW(game.food.row), G2S_COL(game.food.col), COLOR_FOOD) ;
    
    return true ;
}

void start_game()
{
    // screen
    printf("\x1b[%dm", COLOR_TEXT) ;
    fwrite(screen, sizeof *screen, SCREEN_LEN, stdout) ;
    if (game.score > game.best_score[game.level]) {
        game.best_score[game.level] = game.score ;
    }
    game.score = 0 ;
    printf("\x1b[1;8H0\x1b[1;30H%d\x1b[1;50H%d", game.level+1, game.best_score[game.level]) ;

    // obstackles
    printf("\x1b[%dm", COLOR_OBSTACKLE) ;
    game.avail_num = 0 ;
    for (int i=0; i<GAME_ROWS; i++) {
        for (int j=0; j<GAME_COLS; j++) {
            int k = i*GAME_COLS + j ;
            char mask = 1 << ( 7-(k%8) ) ;
            bool is_obst = level_maps[game.level][k/8] & mask ;

            struct pos p={.row=i, .col=j}, z=arr2grid(game.avail_num) ;
            if (is_obst) {
                game.avail_pos[i][j] = arr2grid(GAME_ROWS*GAME_COLS) ;
                printf("\x1b[%d;%dH▆𜵈", G2S_ROW(i), G2S_COL(j)) ;
            } else {
                game.avail_pos[i][j] = z ;
                game.avail[z.row][z.col] = p ;
                game.avail_num++ ;
            }
        }
    }

    // Put snake 
    game.head.row = GAME_ROWS/2 ;
    game.head.col = GAME_COLS/2-4 ;

    game.tail = game.head ;

    struct pos nz = arr2grid(game.avail_num-1) ;
    struct pos pi = game.avail_pos[game.head.row][game.head.col] ;
    struct pos znz = game.avail[nz.row][nz.col] ;
    game.avail_pos[game.head.row][game.head.col] = nz ;
    game.avail_pos[znz.row][znz.col] = pi ;
    game.avail[nz.row][nz.col] = game.tail ;
    game.avail[pi.row][pi.col] = znz ;

    game.dir_new = DIR_RIGHT ;
    game.dir_old = DIR_RIGHT ;

    game.avail_num-- ;

    for (int i=0; i<3; i++) {
        put_head() ;
    }

    generate_food() ;
    fflush(stdout) ;
}

void advance_game()
{
    if (!put_head()) {
        // game over
        printf("\x1b[%dm", COLOR_TEXT) ;
        fwrite(game_over, sizeof *game_over, GAME_OVER_LEN, stdout) ;
        sleep(1) ;
        start_game() ;
        return ;
    }
    if (game.head.row == game.food.row && game.head.col == game.food.col) {
        game.score ++ ;
        printf("\x1b[97m\x1b[1;8H%d", game.score) ;
        if (!generate_food()) {
            // game clear
            printf("\x1b[%dm", COLOR_TEXT) ;
            fwrite(level_clear, sizeof *level_clear, LEVEL_CLEAR_LEN, stdout) ;
            sleep(1) ;
            start_game() ;
            return ;
        }
    } else {
        cut_tail() ;
    }
    fflush(stdout) ;
}

void process_input(TermKeyKey key)
{
    if (key.type == TERMKEY_TYPE_UNICODE) {
        switch (key.code.codepoint) {
            case 'q' :
                exit(EXIT_SUCCESS) ;
                break ;
            case 'n' :
                if (game.score > game.best_score[game.level]) {
                    game.best_score[game.level] = game.score ;
                }
                game.score = 0 ;
                game.level++ ;
                game.level = (LEVELS + game.level) % LEVELS ;
                start_game() ;
                break ;
            case 'p' :
                if (game.score > game.best_score[game.level]) {
                    game.best_score[game.level] = game.score ;
                }
                game.score = 0 ;
                game.level-- ;
                game.level = (LEVELS + game.level) % LEVELS ;
                start_game() ;
                break ;
            default :
                break;
        }
    } else if (key.type == TERMKEY_TYPE_KEYSYM) {
        switch (key.code.sym) {
            case TERMKEY_SYM_UP :
                if (game.dir_new != DIR_DOWN) {
                    game.dir_new = DIR_UP ;
                }
                break ;
            case TERMKEY_SYM_DOWN :
                if (game.dir_new != DIR_UP) {
                    game.dir_new = DIR_DOWN ;
                }
                break ;
            case TERMKEY_SYM_LEFT :
                if (game.dir_new != DIR_RIGHT) {
                    game.dir_new = DIR_LEFT ;
                }
                break ;
            case TERMKEY_SYM_RIGHT :
                if (game.dir_new != DIR_LEFT) {
                    game.dir_new = DIR_RIGHT ;
                }
                break ;
            default :
                break ;
        }
    }
}

void check_term_size()
{
    struct winsize ws ;

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) ;

    if (ws.ws_row < MIN_SCREEN_ROWS || ws.ws_col < MIN_SCREEN_COLS) {
        printf("Terminal should be atleast %d rows by %d cols\n", MIN_SCREEN_ROWS, MIN_SCREEN_COLS) ;
        exit(EXIT_FAILURE) ;
    }
}

int main()
{
    check_term_size() ;

    if (
            atexit(restore_terminal) ||
            signal(SIGINT , handle_exit_signal) == SIG_ERR ||
            signal(SIGTERM, handle_exit_signal) == SIG_ERR ||
            signal(SIGABRT, handle_exit_signal) == SIG_ERR
       ) return EXIT_FAILURE ;

    bool running = true ;

    init_terminal() ;
    start_game() ;

    while (running) {
        struct timespec ts_start ;
        clock_gettime(CLOCK_MONOTONIC, &ts_start) ;

        termkey_advisereadable(tk) ;
        TermKeyKey key ;
        TermKeyResult res ;
        if ((res=termkey_getkey(tk, &key)) == TERMKEY_RES_KEY) {
            process_input(key) ;
        }

        advance_game() ;

        struct timespec ts_end ;
        clock_gettime(CLOCK_MONOTONIC, &ts_end) ;

        struct timespec ts_elapsed = time_diff(ts_start, ts_end) ;
        struct timespec ts_tick = {.tv_sec=0L, .tv_nsec=120000000L} ;
        struct timespec ts_sleep = time_diff(ts_elapsed, ts_tick) ;
        nanosleep(&ts_sleep, NULL) ;
    }

    return EXIT_SUCCESS ;
}
