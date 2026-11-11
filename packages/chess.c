// @version 1.0
// @description Chess against the computer (e2e4 moves)
// chess.c - play chess against MyOS.
//   chess          you play white
//   chess black    you play black
//   chess demo     the computer plays itself
// Moves are typed as from-square to-square: e2e4, g1f3, e7e8q (promotion),
// e1g1 (castling). Commands: help, moves, undo, hint, level 1-6, new, quit.
//
// The engine: a 10x12 mailbox board, alpha-beta search with quiescence and
// iterative deepening against the clock, and piece-square tables for the
// evaluation. Only myos.h is needed (no C library).
#include "myos.h"

enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING, OFF = 7 };
#define BLACK 8
#define COLOR(p) ((p) & BLACK)
#define TYPE(p) ((p) & 7)
#define SQ(file, rank) (21 + (file) + 10 * (rank))
#define FILE_OF(sq) ((sq) % 10 - 1)
#define RANK_OF(sq) ((sq) / 10 - 2)
#define INF 1000000
#define MATE 100000

/* a move packed into an int: from | to << 7 | promotion << 14 | flags << 17 */
#define M_FROM(m) ((m) & 127)
#define M_TO(m) (((m) >> 7) & 127)
#define M_PROMO(m) (((m) >> 14) & 7)
#define M_FLAGS(m) ((m) >> 17)
enum { F_CAPTURE = 1, F_EP = 2, F_CASTLE = 4, F_DOUBLE = 8 };
#define MOVE(f, t, p, fl) ((f) | (t) << 7 | (p) << 14 | (fl) << 17)

static int board[120];
static int side;                /* 0 = white to move, BLACK = black */
static int castle;              /* 1 white short, 2 white long, 4 black short, 8 black long */
static int ep;                  /* en-passant target square or 0 */
static int king_sq[2];
static int ply;

typedef struct { int move, captured, castle, ep; } undo_t;
static undo_t history[1024];
static int hist_len;

static const int knight_d[8] = { 21, 19, 12, 8, -21, -19, -12, -8 };
static const int king_d[8] = { 1, -1, 10, -10, 11, 9, -11, -9 };
static const int value[7] = { 0, 100, 320, 330, 500, 900, 0 };

/* piece-square tables, white's view, a8 first (Michniewski's simplified evaluation) */
static const signed char pst[7][64] = {
    { 0 },
    {  0,  0,  0,  0,  0,  0,  0,  0, 50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10,
       5,  5, 10, 25, 25, 10,  5,  5,  0,  0,  0, 20, 20,  0,  0,  0,  5, -5,-10,  0,  0,-10, -5,  5,
       5, 10, 10,-20,-20, 10, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0 },
    {-50,-40,-30,-30,-30,-30,-40,-50,-40,-20,  0,  0,  0,  0,-20,-40,-30,  0, 10, 15, 15, 10,  0,-30,
     -30,  5, 15, 20, 20, 15,  5,-30,-30,  0, 15, 20, 20, 15,  0,-30,-30,  5, 10, 15, 15, 10,  5,-30,
     -40,-20,  0,  5,  5,  0,-20,-40,-50,-40,-30,-30,-30,-30,-40,-50 },
    {-20,-10,-10,-10,-10,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,-10,  0,  5, 10, 10,  5,  0,-10,
     -10,  5,  5, 10, 10,  5,  5,-10,-10,  0, 10, 10, 10, 10,  0,-10,-10, 10, 10, 10, 10, 10, 10,-10,
     -10,  5,  0,  0,  0,  0,  5,-10,-20,-10,-10,-10,-10,-10,-10,-20 },
    {  0,  0,  0,  0,  0,  0,  0,  0,  5, 10, 10, 10, 10, 10, 10,  5, -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,  0,  0,  0,  5,  5,  0,  0,  0 },
    {-20,-10,-10, -5, -5,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,-10,  0,  5,  5,  5,  5,  0,-10,
      -5,  0,  5,  5,  5,  5,  0, -5,  0,  0,  5,  5,  5,  5,  0, -5,-10,  5,  5,  5,  5,  5,  0,-10,
     -10,  0,  5,  0,  0,  0,  0,-10,-20,-10,-10, -5, -5,-10,-10,-20 },
    {-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,-20,-30,-30,-40,-40,-30,-30,-20,-10,-20,-20,-20,-20,-20,-20,-10,
      20, 20,  0,  0,  0,  0, 20, 20, 20, 30, 10,  0,  0, 10, 30, 20 },
};
/* the king in the endgame: go to the middle */
static const signed char king_end[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,-30,-20,-10,  0,  0,-10,-20,-30,-30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,-30,-10, 30, 40, 40, 30,-10,-30,-30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,-50,-30,-30,-30,-30,-30,-30,-50 };

/* ------------------------------------------------------------ output */
static char outbuf[4096];
static int outlen;

static void flush(void) {
    outbuf[outlen] = 0;
    if (outlen) sys_print(outbuf);
    outlen = 0;
}
static void puts_(const char *s) {
    while (*s) {
        if (outlen >= (int)sizeof outbuf - 1) flush();
        outbuf[outlen++] = *s++;
    }
}
static void putc_(char c) {
    char s[2] = { c, 0 };
    puts_(s);
}
static void putnum(long n) {
    char tmp[24];
    int i = 0;
    if (n < 0) {
        putc_('-');
        n = -n;
    }
    do tmp[i++] = (char)('0' + n % 10);
    while ((n /= 10) && i < 23);
    while (i) putc_(tmp[--i]);
}

static int colors = 1;

/* ------------------------------------------------------------ the position */
static void new_game(void) {
    static const int back[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
    for (int i = 0; i < 120; i++) board[i] = OFF;
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++) board[SQ(f, r)] = EMPTY;
    for (int f = 0; f < 8; f++) {
        board[SQ(f, 0)] = back[f];
        board[SQ(f, 1)] = PAWN;
        board[SQ(f, 6)] = PAWN | BLACK;
        board[SQ(f, 7)] = back[f] | BLACK;
    }
    side = 0;
    castle = 15;
    ep = 0;
    king_sq[0] = SQ(4, 0);
    king_sq[1] = SQ(4, 7);
    hist_len = 0;
}

/* is square sq attacked by the side 'by' (0 or BLACK)? */
static int attacked(int sq, int by) {
    int pawn_dir = by ? -10 : 10;                       /* attacking pawns stand behind */
    if (board[sq - pawn_dir + 1] == (PAWN | by) || board[sq - pawn_dir - 1] == (PAWN | by)) return 1;
    for (int i = 0; i < 8; i++) {
        if (board[sq + knight_d[i]] == (KNIGHT | by)) return 1;
        if (board[sq + king_d[i]] == (KING | by)) return 1;
    }
    for (int i = 0; i < 8; i++) {
        int d = king_d[i], s = sq + d;
        while (board[s] == EMPTY) s += d;
        int p = board[s];
        if (p == OFF || COLOR(p) != by) continue;
        if (TYPE(p) == QUEEN) return 1;
        if (i < 4 ? TYPE(p) == ROOK : TYPE(p) == BISHOP) return 1;
    }
    return 0;
}

static int in_check(int who) {
    return attacked(king_sq[who ? 1 : 0], who ^ BLACK);
}

/* all moves (some may leave the king in check); captures_only for quiescence */
static int gen_moves(int *list, int captures_only) {
    int n = 0, us = side, them = side ^ BLACK;
    int up = us ? -10 : 10, start_rank = us ? 6 : 1, last_rank = us ? 0 : 7;
    for (int sq = 21; sq <= 98; sq++) {
        int p = board[sq];
        if (p == EMPTY || p == OFF || COLOR(p) != us) continue;
        int t = TYPE(p);
        if (t == PAWN) {
            int to = sq + up;
            for (int k = 0; k < 2; k++) {               /* captures */
                int c = to + (k ? 1 : -1);
                int fl = (board[c] != EMPTY && board[c] != OFF && COLOR(board[c]) == them) ? F_CAPTURE
                       : (c == ep && ep) ? F_CAPTURE | F_EP : 0;
                if (!fl) continue;
                if (RANK_OF(c) == last_rank)
                    for (int pr = QUEEN; pr >= KNIGHT; pr--) list[n++] = MOVE(sq, c, pr, fl);
                else list[n++] = MOVE(sq, c, 0, fl);
            }
            if (board[to] == EMPTY) {
                if (RANK_OF(to) == last_rank) {
                    for (int pr = QUEEN; pr >= KNIGHT; pr--)
                        if (!captures_only || pr == QUEEN) list[n++] = MOVE(sq, to, pr, 0);
                } else if (!captures_only) {
                    list[n++] = MOVE(sq, to, 0, 0);
                    if (RANK_OF(sq) == start_rank && board[to + up] == EMPTY)
                        list[n++] = MOVE(sq, to + up, 0, F_DOUBLE);
                }
            }
            continue;
        }
        const int *dirs = t == KNIGHT ? knight_d : king_d;
        int nd = 8, from_d = 0, slide = t == BISHOP || t == ROOK || t == QUEEN;
        if (t == BISHOP) from_d = 4;
        if (t == ROOK) nd = 4;
        for (int i = from_d; i < nd; i++) {
            int d = dirs[i], to = sq + d;
            for (;;) {
                int q = board[to];
                if (q == OFF || (q != EMPTY && COLOR(q) == us)) break;
                if (q != EMPTY) {
                    list[n++] = MOVE(sq, to, 0, F_CAPTURE);
                    break;
                }
                if (!captures_only) list[n++] = MOVE(sq, to, 0, 0);
                if (!slide) break;
                to += d;
            }
        }
        if (t == KING && !captures_only) {              /* castling */
            int r = us ? 7 : 0, base = us ? 4 : 1;
            if ((castle & base) && board[SQ(5, r)] == EMPTY && board[SQ(6, r)] == EMPTY &&
                board[SQ(7, r)] == (ROOK | us) && !attacked(SQ(4, r), them) && !attacked(SQ(5, r), them) &&
                !attacked(SQ(6, r), them))
                list[n++] = MOVE(sq, SQ(6, r), 0, F_CASTLE);
            if ((castle & (base * 2)) && board[SQ(3, r)] == EMPTY && board[SQ(2, r)] == EMPTY &&
                board[SQ(1, r)] == EMPTY && board[SQ(0, r)] == (ROOK | us) && !attacked(SQ(4, r), them) &&
                !attacked(SQ(3, r), them) && !attacked(SQ(2, r), them))
                list[n++] = MOVE(sq, SQ(2, r), 0, F_CASTLE);
        }
    }
    return n;
}

/* castling rights lost when something moves from/to these squares */
static int castle_mask(int sq) {
    if (sq == SQ(4, 0)) return 3;
    if (sq == SQ(7, 0)) return 1;
    if (sq == SQ(0, 0)) return 2;
    if (sq == SQ(4, 7)) return 12;
    if (sq == SQ(7, 7)) return 4;
    if (sq == SQ(0, 7)) return 8;
    return 0;
}

static void undo_move(void);

/* make a move; returns 0 (and takes it back) if it leaves our king in check */
static int make_move(int m) {
    int from = M_FROM(m), to = M_TO(m), fl = M_FLAGS(m), p = board[from];
    undo_t *u = &history[hist_len++];
    u->move = m;
    u->castle = castle;
    u->ep = ep;
    u->captured = board[to];
    if (fl & F_EP) {
        int cap = to + (side ? 10 : -10);
        u->captured = board[cap];
        board[cap] = EMPTY;
    }
    board[to] = M_PROMO(m) ? (M_PROMO(m) | side) : p;
    board[from] = EMPTY;
    if (fl & F_CASTLE) {
        int r = side ? 7 : 0;
        if (FILE_OF(to) == 6) {
            board[SQ(5, r)] = board[SQ(7, r)];
            board[SQ(7, r)] = EMPTY;
        } else {
            board[SQ(3, r)] = board[SQ(0, r)];
            board[SQ(0, r)] = EMPTY;
        }
    }
    if (TYPE(p) == KING) king_sq[side ? 1 : 0] = to;
    castle &= ~(castle_mask(from) | castle_mask(to));
    ep = (fl & F_DOUBLE) ? (from + to) / 2 : 0;
    side ^= BLACK;
    ply++;
    if (in_check(side ^ BLACK)) {
        undo_move();
        return 0;
    }
    return 1;
}

static void undo_move(void) {
    undo_t *u = &history[--hist_len];
    int m = u->move, from = M_FROM(m), to = M_TO(m), fl = M_FLAGS(m);
    side ^= BLACK;
    ply--;
    int p = M_PROMO(m) ? (PAWN | side) : board[to];
    board[from] = p;
    board[to] = u->captured;
    if (fl & F_EP) {
        board[to] = EMPTY;
        board[to + (side ? 10 : -10)] = u->captured;
    }
    if (fl & F_CASTLE) {
        int r = side ? 7 : 0;
        if (FILE_OF(to) == 6) {
            board[SQ(7, r)] = board[SQ(5, r)];
            board[SQ(5, r)] = EMPTY;
        } else {
            board[SQ(0, r)] = board[SQ(3, r)];
            board[SQ(3, r)] = EMPTY;
        }
    }
    if (TYPE(p) == KING) king_sq[side ? 1 : 0] = from;
    castle = u->castle;
    ep = u->ep;
}

static int legal_moves(int *list) {
    int all[256], n = gen_moves(all, 0), k = 0;
    for (int i = 0; i < n; i++) {
        if (make_move(all[i])) {
            undo_move();
            list[k++] = all[i];
        }
    }
    return k;
}

/* ------------------------------------------------------------ evaluation and search */
static int evaluate(void) {
    int score = 0, material = 0;
    for (int sq = 21; sq <= 98; sq++) {
        int p = board[sq];
        if (p != EMPTY && p != OFF && TYPE(p) != KING && TYPE(p) != PAWN) material += value[TYPE(p)];
    }
    int endgame = material <= 1300;
    for (int sq = 21; sq <= 98; sq++) {
        int p = board[sq];
        if (p == EMPTY || p == OFF) continue;
        int t = TYPE(p), f = FILE_OF(sq), r = RANK_OF(sq);
        int idx = COLOR(p) ? r * 8 + f : (7 - r) * 8 + f;   /* tables are from white's side, a8 first */
        int v = value[t] + (t == KING && endgame ? king_end[idx] : pst[t][idx]);
        score += COLOR(p) ? -v : v;
    }
    return side ? -score : score;
}

static long nodes;
static unsigned long stop_at;
static int stopped;
static int killer[64][2];

static int order_score(int m, int depth) {
    if (M_FLAGS(m) & F_CAPTURE) {
        int victim = TYPE(board[M_TO(m)]);
        if (M_FLAGS(m) & F_EP) victim = PAWN;
        return 10000 + value[victim] * 10 - value[TYPE(board[M_FROM(m)])] / 10;
    }
    if (M_PROMO(m)) return 9000;
    if (depth < 64 && (m == killer[depth][0] || m == killer[depth][1])) return 8000;
    return 0;
}

static void sort_moves(int *list, int n, int depth, int first) {
    int score[256];
    for (int i = 0; i < n; i++) score[i] = list[i] == first ? 100000 : order_score(list[i], depth);
    for (int i = 1; i < n; i++) {                       /* insertion sort: lists are short */
        int m = list[i], s = score[i], j = i - 1;
        while (j >= 0 && score[j] < s) {
            list[j + 1] = list[j];
            score[j + 1] = score[j];
            j--;
        }
        list[j + 1] = m;
        score[j + 1] = s;
    }
}

static void check_time(void) {
    if ((++nodes & 1023) == 0 && sys_ticks() >= stop_at) stopped = 1;
}

static int quiesce(int alpha, int beta, int qdepth) {
    check_time();
    int stand = evaluate();
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    if (qdepth > 8) return alpha;
    int list[256], n = gen_moves(list, 1);
    sort_moves(list, n, 63, 0);
    for (int i = 0; i < n && !stopped; i++) {
        if (!make_move(list[i])) continue;
        int s = -quiesce(-beta, -alpha, qdepth + 1);
        undo_move();
        if (s >= beta) return beta;
        if (s > alpha) alpha = s;
    }
    return alpha;
}

static int best_root;

static int search(int depth, int alpha, int beta, int from_root, int first) {
    if (depth <= 0) return quiesce(alpha, beta, 0);
    check_time();
    int list[256], n = gen_moves(list, 0), legal = 0, check = in_check(side);
    if (check) depth++;                                 /* look further when in check */
    sort_moves(list, n, from_root, first);
    for (int i = 0; i < n && !stopped; i++) {
        if (!make_move(list[i])) continue;
        legal++;
        int s = -search(depth - 1, -beta, -alpha, from_root + 1, 0);
        undo_move();
        if (stopped) break;
        if (s > alpha) {
            alpha = s;
            if (from_root == 0) best_root = list[i];
            if (s >= beta) {
                if (!(M_FLAGS(list[i]) & F_CAPTURE) && from_root < 64) {
                    killer[from_root][1] = killer[from_root][0];
                    killer[from_root][0] = list[i];
                }
                return beta;
            }
        }
    }
    if (!legal && !stopped) return check ? -MATE + from_root : 0;
    return alpha;
}

static int max_depth = 5;
static int think_ticks = 400;       /* 4 s at most */

/* iterative deepening; returns the best move (0 if none) */
static int think(int show) {
    int list[256];
    if (!legal_moves(list)) return 0;
    int best = list[0];
    nodes = 0;
    stopped = 0;
    stop_at = sys_ticks() + (unsigned long)think_ticks;
    for (int i = 0; i < 64; i++) killer[i][0] = killer[i][1] = 0;
    for (int d = 1; d <= max_depth; d++) {
        best_root = 0;
        int s = search(d, -INF, INF, 0, best);
        if (stopped) break;
        if (best_root) best = best_root;
        if (show) {
            puts_("  depth ");
            putnum(d);
            puts_("  score ");
            putnum(s);
            puts_("\n");
            flush();
        }
        if (s > MATE - 100 || s < -MATE + 100) break;   /* found a mate */
    }
    return best;
}

/* ------------------------------------------------------------ text */
static void square_name(int sq, char *out) {
    out[0] = (char)('a' + FILE_OF(sq));
    out[1] = (char)('1' + RANK_OF(sq));
    out[2] = 0;
}

static void move_name(int m, char *out) {
    square_name(M_FROM(m), out);
    square_name(M_TO(m), out + 2);
    if (M_PROMO(m)) {
        out[4] = " pnbrqk"[M_PROMO(m)];
        out[5] = 0;
    }
}

static int parse_move(const char *s, int *list, int n) {
    while (*s == ' ') s++;
    if (s[0] == 'O' || s[0] == 'o' || s[0] == '0') {                /* O-O, O-O-O */
        int lng = (s[3] == '-');
        for (int i = 0; i < n; i++)
            if ((M_FLAGS(list[i]) & F_CASTLE) && (FILE_OF(M_TO(list[i])) == (lng ? 2 : 6))) return list[i];
        return 0;
    }
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return 0;
    if (s[2] == '-' || s[2] == ' ' || s[2] == 'x') s++;             /* e2-e4 */
    if (s[2] < 'a' || s[2] > 'h' || s[3] < '1' || s[3] > '8') return 0;
    int from = SQ(s[0] - 'a', s[1] - '1'), to = SQ(s[2] - 'a', s[3] - '1');
    int promo = QUEEN;
    char c = s[4] == '=' ? s[5] : s[4];
    if (c == 'n' || c == 'N') promo = KNIGHT;
    if (c == 'b' || c == 'B') promo = BISHOP;
    if (c == 'r' || c == 'R') promo = ROOK;
    for (int i = 0; i < n; i++) {
        int m = list[i];
        if (M_FROM(m) == from && M_TO(m) == to && (!M_PROMO(m) || M_PROMO(m) == promo)) return m;
    }
    return 0;
}

static void show_board(int flipped, int last) {
    puts_(flipped ? "\n     h  g  f  e  d  c  b  a\n" : "\n     a  b  c  d  e  f  g  h\n");
    for (int rr = 7; rr >= 0; rr--) {
        int r = flipped ? 7 - rr : rr;
        puts_("  ");
        putc_((char)('1' + r));
        puts_(" ");
        for (int ff = 0; ff < 8; ff++) {
            int f = flipped ? 7 - ff : ff, sq = SQ(f, r), p = board[sq];
            int light = (f + r) & 1;
            int mark = last && (sq == M_FROM(last) || sq == M_TO(last));
            char ch = p == EMPTY ? (colors ? ' ' : (light ? '.' : ':')) : " PNBRQK"[TYPE(p)];
            if (p != EMPTY && COLOR(p)) ch = (char)(ch + 32);
            if (colors) {
                puts_(mark ? (light ? "\x1b[48;5;186m" : "\x1b[48;5;143m") : (light ? "\x1b[48;5;223m" : "\x1b[48;5;137m"));
                puts_(p != EMPTY && COLOR(p) ? "\x1b[1;30m" : "\x1b[1;97m");
                putc_(' ');
                putc_(ch);
                putc_(' ');
                puts_("\x1b[0m");
            } else {
                putc_(' ');
                putc_(ch);
                putc_(' ');
            }
        }
        puts_(" ");
        putc_((char)('1' + r));
        puts_("\n");
    }
    puts_(flipped ? "     h  g  f  e  d  c  b  a\n" : "     a  b  c  d  e  f  g  h\n");
    flush();
}

/* is the game over? prints why; 1 = yes */
static int game_over(void) {
    int list[256];
    if (legal_moves(list)) {
        int minor = 0, other = 0;                       /* dead positions: K v K, K+minor v K */
        for (int sq = 21; sq <= 98; sq++) {
            int t = TYPE(board[sq]);
            if (board[sq] == OFF || board[sq] == EMPTY || t == KING) continue;
            if (t == KNIGHT || t == BISHOP) minor++;
            else other++;
        }
        if (!other && minor <= 1) {
            puts_("Draw: not enough pieces to checkmate.\n");
            flush();
            return 1;
        }
        return 0;
    }
    if (in_check(side)) puts_(side ? "Checkmate! White wins.\n" : "Checkmate! Black wins.\n");
    else puts_("Stalemate - a draw.\n");
    flush();
    return 1;
}

static int computer_move(void) {
    puts_("Thinking...\n");
    flush();
    unsigned long t0 = sys_ticks();
    int m = think(0);
    if (!m) return 0;
    char name[8];
    move_name(m, name);
    make_move(m);
    puts_("I play ");
    puts_(name);
    puts_(in_check(side) ? "  Check!" : "");
    puts_("   (");
    putnum(nodes);
    puts_(" positions, ");
    putnum((long)(sys_ticks() - t0) / 100);
    putc_('.');
    putnum((long)(sys_ticks() - t0) / 10 % 10);
    puts_(" s)\n");
    flush();
    return m;
}

static void help(void) {
    puts_("Type a move as from-square to-square, e.g. e2e4 or g1f3.\n"
          "  e7e8q  promote (q r b n)     e1g1 or O-O  castle\n"
          "Commands: moves  hint  undo  level 1-6  flip  colors  new  quit\n");
    flush();
}

static int str_eq(const char *a, const char *b) {
    while (*a == ' ') a++;
    while (*b && *a == *b) a++, b++;
    while (*a == ' ') a++;
    return !*a && !*b;
}

static int starts(const char *a, const char *b) {
    while (*a == ' ') a++;
    while (*b && *a == *b) a++, b++;
    return !*b;
}

int main(const char *args) {
    int human = 0, demo = 0, flipped = 0, last = 0;
    for (const char *a = args; a && *a; a++) {         /* chess black plain, chess demo ... */
        if (a != args && a[-1] != ' ') continue;
        if (starts(a, "black")) human = BLACK, flipped = 1;
        if (starts(a, "demo")) demo = 1, think_ticks = 100;
        if (starts(a, "plain")) colors = 0;
    }
    new_game();
    puts_("MyOS Chess - you are ");
    puts_(demo ? "watching" : human ? "black" : "white");
    puts_(". Type 'help' for help.\n");
    flush();
    for (;;) {
        show_board(flipped, last);
        if (game_over()) {
            if (demo) return 0;
            puts_("Type 'new' for another game, 'undo' or 'quit'.\n");
            flush();
        } else if (demo || side != human) {
            last = computer_move();
            if (demo && hist_len >= 200) {
                puts_("That's enough for a demo.\n");
                flush();
                return 0;
            }
            continue;
        }
        if (in_check(side) && !demo) {
            puts_("You are in check.\n");
            flush();
        }
        char line[64];
        for (;;) {
            puts_(side ? "Black" : "White");
            puts_(" to move> ");
            flush();
            long n = sys_readline(line, sizeof line - 1);
            if (n < 0) n = 0;
            line[n] = 0;
            for (int i = 0; line[i]; i++)
                if (line[i] >= 'A' && line[i] <= 'Z' && line[i] != 'O') line[i] = (char)(line[i] + 32);
            int list[256], nl = legal_moves(list);
            if (str_eq(line, "quit") || str_eq(line, "q") || str_eq(line, "exit")) return 0;
            if (str_eq(line, "help") || str_eq(line, "?")) { help(); continue; }
            if (str_eq(line, "new")) { new_game(); last = 0; break; }
            if (str_eq(line, "flip")) { flipped = !flipped; break; }
            if (str_eq(line, "colors") || str_eq(line, "colours")) { colors = !colors; break; }
            if (str_eq(line, "moves")) {
                char name[8];
                for (int i = 0; i < nl; i++) {
                    move_name(list[i], name);
                    puts_(name);
                    puts_(i % 12 == 11 ? "\n" : " ");
                }
                puts_("\n");
                flush();
                continue;
            }
            if (str_eq(line, "undo")) {                 /* take back the computer's move and ours */
                if (hist_len >= 2) { undo_move(); undo_move(); }
                else if (hist_len == 1 && human) { /* computer moved first: nothing of ours to undo */ }
                last = 0;
                break;
            }
            if (str_eq(line, "hint")) {
                int saved = think_ticks;
                think_ticks = 100;
                int m = think(0);
                think_ticks = saved;
                char name[8];
                if (m) {
                    move_name(m, name);
                    puts_("Try ");
                    puts_(name);
                    puts_("\n");
                    flush();
                }
                continue;
            }
            if (starts(line, "level")) {
                const char *p = line;
                while (*p && (*p < '0' || *p > '9')) p++;
                int lv = *p ? *p - '0' : 0;
                if (lv >= 1 && lv <= 6) {
                    max_depth = lv;
                    think_ticks = lv * 100;
                    puts_("Level ");
                    putnum(lv);
                    puts_(" (searches ");
                    putnum(lv);
                    puts_(" moves ahead)\n");
                } else {
                    puts_("Level is 1 (easy) to 6 (strong). Now: ");
                    putnum(max_depth);
                    puts_("\n");
                }
                flush();
                continue;
            }
            if (!line[0]) continue;
            int m = parse_move(line, list, nl);
            if (!m) {
                puts_("That's not a legal move here. Type 'moves' to see them, 'help' for help.\n");
                flush();
                continue;
            }
            make_move(m);
            last = m;
            break;
        }
    }
}
