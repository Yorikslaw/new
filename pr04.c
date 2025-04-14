#include <errno.h>
#include <myBigChars.h>
#include <myReadKey.h>
#include <mySimpleComputer.h>
#include <myTerm.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


#define CACHE_SIZE 5
#define READ 10 // отвечает за чтение ячейки памяти из in-out
#define WRITE 11 // отвечает за вывод ячейки памяти в in_out

#define LOAD 20 // сохранения значения ячейки в аккумулятор
#define STORE \
    21 // выгружает значение из аккумулятора по указанному адресу памяти

#define ADD \
    30 // выполняет сложение слова в аккумуляторе и слова из указанной ячейки
       // памяти
#define SUB \
    31 // вычитает из слова в аккумуляторе слово из указанной ячейки памяти
#define DIVIDE \
    32 // выполняет деление слова в аккумуляторе на слово из указанной ячейки
       // памяти
#define MUL \
    33 // вычисляет произведение слова в аккумуляторее на слово из указанной
       // ячейки памяти

#define HALT 43 // выполняется при завершении программы

#define JNP \
    59 // переход к указанному адресу памяти, если результат предыдущей операции
       // нечетный
#define CHL \
    60 // логический двоичный сдвиг содержимого указанной ячейки памяти влево
#define HISTORY_LENGTH 10

int cell;
int numStrForLogs;
int term_adress[5];
int term_input[5];

typedef struct {
    int address;
    int value;
    int valid;
} CacheLine;

CacheLine cache[CACHE_SIZE];

int history[HISTORY_LENGTH] = {0};

void sighandler(int sig)
{
    mt_gotoXY(26, 1);
    printf("Получен сигнал - %d\n", sig);
}

int printMemory()
{
    if (bc_box(1, 1, 15, 61, WHITE, BLACK, "Оперативная память", RED, BLACK)
        != 0) {
        return -1;
    }

    for (int i = 0; i < SIZE; i++) {
        sc_printCell(i, WHITE, BLACK);
    }

    return 0;
}

int printAccumulator()
{
    if (bc_box(1, 62, 3, 25, WHITE, BLACK, "Аккумулятор", RED, BLACK) != 0) {
        return -1;
    }

    mt_setdefaultcolor();

    mt_gotoXY(2, 65);
    sc_printAccumulator();

    return 0;
}

int printFlags()
{
    if (bc_box(1, 87, 3, 25, WHITE, BLACK, "Регистр флагов", RED, BLACK) != 0) {
        return -1;
    }

    sc_printFlags(2, 92);

    return 0;
}

int printICounter()
{
    if (bc_box(4, 62, 3, 25, WHITE, BLACK, "Счетчик команд", RED, BLACK) != 0) {
        return -1;
    }

    sc_printCounters(5, 65);
    return 0;
}

int printCommand(int address)
{
    if (bc_box(4, 87, 3, 25, WHITE, BLACK, "Команда", RED, BLACK) != 0) {
        return -1;
    }

    int value = 0;
    sc_memoryGet(address, &value);

    sc_printCommand(value, 5, 95);

    return 0;
}

int printRedactedFormat(int address)
{
    if (bc_box(16,
               1,
               3,
               61,
               WHITE,
               BLACK,
               "Редактируемая ячейка (формат)",
               RED,
               WHITE)
        != 0) {
        return -1;
    }

    int value = 0;
    cache_memoryGet(address, &value);
    sc_printDecodedCommand(value, 17, 2);
    mt_setdefaultcolor();

    return 0;
}

int printRedacted()
{
    if (bc_box(7,
               62,
               12,
               50,
               WHITE,
               BLACK,
               "Редактируемая ячейка (увеличено)",
               RED,
               WHITE)
        != 0) {
        return -1;
    }

    return 0;
}

// FIFO-указатель
int cache_pointer = 0;

// Задержка процессора (эмуляция простоя)
void cpu_stall() {
    usleep(50000); // 50 мс задержка при обращении к основной памяти
}

// Очистка кэша
void cache_clear() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        cache[i].valid = 0;
        cache[i].address = -1;
    }
}

// Поиск адреса в кэше
int cache_find(int address) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].address == address) {
            return i;
        }
    }
    return -1;
}

// Загрузка значения из кэша или памяти
int cache_memoryGet(int address, int *value) {
    int index = cache_find(address);
    if (index != -1) {
        *value = cache[index].value;
        return 0; // Кэш-хит
    } else {
        // Кэш-промах — читаем из памяти
        cpu_stall(); // процессор ждёт
        if (sc_memoryGet(address, value) != 0)
            return -1;

        // Сохраняем в кэш
        cache[cache_pointer].address = address;
        cache[cache_pointer].value = *value;
        cache[cache_pointer].valid = 1;
        cache_pointer = (cache_pointer + 1) % CACHE_SIZE;

        return 1; // Кэш-промах
    }
}

// Сохранение значения с учётом кэша
int cache_memorySet(int address, int value) {
    int index = cache_find(address);
    if (index != -1) {
        cache[index].value = value;
    } else {
        // Если нет в кэше — добавляем
        cpu_stall(); // имитация ожидания
        cache[cache_pointer].address = address;
        cache[cache_pointer].value = value;
        cache[cache_pointer].valid = 1;
        cache_pointer = (cache_pointer + 1) % CACHE_SIZE;
    }

    return sc_memorySet(address, value); // всегда записываем в память
}

int printCache() {
    if (bc_box(19, 1, 7, 68, WHITE, BLACK, "Кэш процессора", GREEN, BLACK) != 0)
        return -1;

    for (int i = 0; i < CACHE_SIZE; i++) {
        mt_gotoXY(20 + i, 2);
        if (cache[i].valid) {
            printf("%02d: адрес=%04d знач=%+05d", i, cache[i].address, cache[i].value);
        } else {
            printf("%02d: [пусто]", i);
        }
    }

    return 0;
}

void add_to_history(int value) {
    for (int i = 0; i < HISTORY_LENGTH - 1; ++i) {
        history[i] = history[i + 1];
    }
    history[HISTORY_LENGTH - 1] = value;
}

void draw_history_window() {
    mt_gotoXY(65, 20);
    printf("IN-OUT: ");
    for (int i = 0; i < HISTORY_LENGTH; ++i) {
        printf("%04X ", history[i] & 0x3FFF); // 14-битное значение
    }
}

int printHelp()
{
    if (bc_box(19, 80, 7, 32, WHITE, BLACK, "Клавиши", GREEN, WHITE) != 0) {
        return -1;
    }

    char* message = "";

    mt_gotoXY(20, 81);
    message = "l - load";
    printf("%s", message);

    mt_gotoXY(20, 81 + bc_strlen(message) + 1);
    message = "s - save";
    printf("%s", message);

    mt_gotoXY(20, 81 + 2 * (bc_strlen(message) + 1));
    message = "i - reset";
    printf("%s", message);

    mt_gotoXY(22, 81);
    message = "ESC - exit";
    printf("%s", message);

    mt_gotoXY(21, 81);
    message = "r - run";
    printf("%s", message);

    mt_gotoXY(21, 81 + bc_strlen(message) + 1);
    message = "t - step";
    printf("%s", message);

    mt_gotoXY(23, 81);
    message = "F5 - accumulator";
    printf("%s", message);

    mt_gotoXY(24, 81);
    message = "F6 - instruction counter";
    printf("%s", message);

    return 0;
}

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0) {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

void addTerm(int address, int input)
{
    for (int i = 0; i < 4; i++) {
        term_adress[i] = term_adress[i + 1];
        term_input[i] = term_input[i + 1];
    }

    term_adress[4] = address;
    term_input[4] = input;
}

void termInit()
{
    for (int i = 0; i < 6; i++) {
        term_input[i] = -1;
        term_adress[i] = 10;
    }
}

void printTerm(int address, int input)
{
    int value = 0;
    cache_memoryGet(address, &value);
    int sign = 0, command = 0, operand = 0;
    sc_commandDecode(value, &sign, &command, &operand);
    printf("%02d%c %c%02x%02x",
           address,
           (input == 1) ? '>' : '<',
           (sign == 0) ? '+' : '-',
           command,
           operand);
}

void printInOutCells()
{
    for (int i = 0; i < 5; i++) {
        mt_setdefaultcolor();
        if (term_input[i] == -1)
            continue;
        mt_gotoXY(20 + i, 70);
        printTerm(term_adress[i], term_input[i]);
    }
}

int printInOut()
{
    if (bc_box(19, 69, 7, 11, WHITE, BLACK, "IN--OUT", GREEN, WHITE) != 0) {
        return -1;
    }

    return 0;
}

int intToHex(int command, int operand, char* str)
{
    if (!str) {
        return 1;
    }

    for (int i = 0; i < 5; i++) {
        str[i] = 0;
    }

    int remainder;
    int whole = command;
    int i;

    for (i = 2; whole >= 16; i++) {
        remainder = whole % 16;
        whole = whole / 16;
        if (remainder == 10) {
            str[i] = 'A';
        } else if (remainder == 11) {
            str[i] = 'B';
        } else if (remainder == 12) {
            str[i] = 'C';
        } else if (remainder == 13) {
            str[i] = 'D';
        } else if (remainder == 14) {
            str[i] = 'E';
        } else if (remainder == 15) {
            str[i] = 'F';
        } else {
            str[i] = remainder + 48;
        }
    }

    if (whole != 0) {
        if (whole == 10) {
            str[i] = 'A';
        } else if (whole == 11) {
            str[i] = 'B';
        } else if (whole == 12) {
            str[i] = 'C';
        } else if (whole == 13) {
            str[i] = 'D';
        } else if (whole == 14) {
            str[i] = 'E';
        } else if (whole == 15) {
            str[i] = 'F';
        } else {
            str[i] = whole + 48;
        }
    }

    whole = operand;

    for (i = 0; whole >= 16; i++) {
        remainder = whole % 16;
        whole = whole / 16;
        if (remainder == 10) {
            str[i] = 'A';
        } else if (remainder == 11) {
            str[i] = 'B';
        } else if (remainder == 12) {
            str[i] = 'C';
        } else if (remainder == 13) {
            str[i] = 'D';
        } else if (remainder == 14) {
            str[i] = 'E';
        } else if (remainder == 15) {
            str[i] = 'F';
        } else {
            str[i] = remainder + 48;
        }
    }

    if (whole != 0) {
        if (whole == 10) {
            str[i] = 'A';
        } else if (whole == 11) {
            str[i] = 'B';
        } else if (whole == 12) {
            str[i] = 'C';
        } else if (whole == 13) {
            str[i] = 'D';
        } else if (whole == 14) {
            str[i] = 'E';
        } else if (whole == 15) {
            str[i] = 'F';
        } else {
            str[i] = whole + 48;
        }
    }

    return 0;
}

int printBigCharInBox(int address)
{
    mt_gotoXY(17, 63);
    mt_setfgcolor(RED);
    printf("Номер редактируемой ячейки %.3d", cell);

    int bigChars[5][2];

    int value = 0;
    cache_memoryGet(address, &value);

    int tmp_number = value;

    int sign = 0, command = 0, operand = 0;
    sc_commandDecode(tmp_number, &sign, &command, &operand);

    if (sign == 0) {
        bigChars[0][0] = bc_Plus(0);
        bigChars[0][1] = bc_Plus(1);
    } else {
        bigChars[0][0] = bc_Minus(0);
        bigChars[0][1] = bc_Minus(1);
    }

    char buf[5];

    if (intToHex(command, operand, buf)) {
        return 1;
    }

    int j = 4;

    for (int i = 0; i < 4; i++) {
        if (buf[i] == '0') {
            fflush(stdout);
            bigChars[j][0] = bc_Null(0);
            bigChars[j][1] = bc_Null(1);
        } else if (buf[i] == '1') {
            bigChars[j][0] = bc_One(0);
            bigChars[j][1] = bc_One(1);
        } else if (buf[i] == '2') {
            bigChars[j][0] = bc_Two(0);
            bigChars[j][1] = bc_Two(1);
        } else if (buf[i] == '3') {
            bigChars[j][0] = bc_Three(0);
            bigChars[j][1] = bc_Three(1);
        } else if (buf[i] == '4') {
            bigChars[j][0] = bc_Four(0);
            bigChars[j][1] = bc_Four(1);
        } else if (buf[i] == '5') {
            bigChars[j][0] = bc_Five(0);
            bigChars[j][1] = bc_Five(1);
        } else if (buf[i] == '6') {
            bigChars[j][0] = bc_Six(0);
            bigChars[j][1] = bc_Six(1);
        } else if (buf[i] == '7') {
            bigChars[j][0] = bc_Seven(0);
            bigChars[j][1] = bc_Seven(1);
        } else if (buf[i] == '8') {
            bigChars[j][0] = bc_Eight(0);
            bigChars[j][1] = bc_Eight(1);
        } else if (buf[i] == '9') {
            bigChars[j][0] = bc_Nine(0);
            bigChars[j][1] = bc_Nine(1);
        } else if (buf[i] == 'A') {
            bigChars[j][0] = bc_A(0);
            bigChars[j][1] = bc_A(1);
        } else if (buf[i] == 'B') {
            bigChars[j][0] = bc_B(0);
            bigChars[j][1] = bc_B(1);
        } else if (buf[i] == 'C') {
            bigChars[j][0] = bc_C(0);
            bigChars[j][1] = bc_C(1);
        } else if (buf[i] == 'D') {
            bigChars[j][0] = bc_D(0);
            bigChars[j][1] = bc_D(1);
        } else if (buf[i] == 'E') {
            bigChars[j][0] = bc_E(0);
            bigChars[j][1] = bc_E(1);
        } else if (buf[i] == 'F') {
            bigChars[j][0] = bc_F(0);
            bigChars[j][1] = bc_F(1);
        } else {
            bigChars[j][0] = bc_Null(0);
            bigChars[j][1] = bc_Null(1);
        }
        j--;
    }

    int x = 63;

    for (int i = 0; i < 5; i++) {
        bc_printbigchar(bigChars[i], x + i * 10, 9, RED, BLACK);
    }

    mt_gotoXY(26, 1);
    mt_setdefaultcolor();
    printf("\n");
    return 0;
}

int changeSizeTerm()
{
    int size_console_x;
    int size_console_y;

    if (mt_getscreensize(&size_console_x, &size_console_y) != 0) {
        printf("Error\n");
        return 1;
    }

    if (size_console_x < 115 || size_console_y < 30) {
        printf("\033[8;30;115t");
    }

    mt_clrscr();
    mt_gotoXY(1, 1);

    return 0;
}

void initNumberCell()
{
    cell = 0;
}

void initNumStrForLogs()
{
    numStrForLogs = 0;
}

void selectCellMemory(enum way w)
{
    enum Colors color = RED;

    if (w == way_RIGHT) {
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

        color = WHITE;
        sc_printCell(cell, WHITE, BLACK);
        color = RED;
        if (cell < SIZE - 1) {
            cell++;
        } else {
            cell = 0;
        }

        color = RED;
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
        mt_setbgcolor(color);

        sc_printCell(cell, WHITE, RED);
        mt_setdefaultcolor();
        printCommand(cell);
        printRedactedFormat(cell);
        printBigCharInBox(cell);
    }

    if (w == way_LEFT) {
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

        color = WHITE;
        sc_printCell(cell, WHITE, BLACK);
        color = RED;

        if (cell > 0) {
            cell--;
        } else {
            cell = SIZE - 1;
        }

        color = RED;
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
        mt_setbgcolor(color);

        color = WHITE;
        sc_printCell(cell, WHITE, RED);
        printCommand(cell);
        mt_setdefaultcolor();
        printRedactedFormat(cell);
        printBigCharInBox(cell);
    }

    if (w == way_UP) {
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

        color = WHITE;
        sc_printCell(cell, WHITE, BLACK);
        color = RED;

        if (cell > 9) {
            cell -= 10;
        } else if (cell == 8 || cell == 9) {
            cell = cell + 110;
        } else {
            cell = cell + 120;
        }

        color = RED;
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

        color = WHITE;
        sc_printCell(cell, WHITE, RED);
        mt_setdefaultcolor();
        printCommand(cell);
        printRedactedFormat(cell);
        printBigCharInBox(cell);
    }

    if (w == way_DOWN) {
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

        color = WHITE;
        sc_printCell(cell, WHITE, BLACK);
        color = RED;

        if (cell < SIZE - 10) {
            cell += 10;
        } else if (cell == 118 || cell == 119) {
            cell = cell - 110;
        } else {
            cell -= 120;
        }

        color = RED;
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
        mt_setbgcolor(color);

        color = WHITE;
        sc_printCell(cell, WHITE, RED);
        mt_setdefaultcolor();
        printCommand(cell);
        printRedactedFormat(cell);
        printBigCharInBox(cell);
    }

    if (w == way_DEFAULT) {
        printBigCharInBox(cell);
        printRedactedFormat(cell);
        color = RED;
        mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
        mt_setbgcolor(color);
        color = WHITE;
        sc_printCell(cell, WHITE, BLACK);
        mt_setdefaultcolor();
        printCommand(cell);
    }

    mt_setdefaultcolor();
}

void selectCellMemoryByNumber(int num)
{
    if (num < 0 || num > 99) {
        return;
    }

    cell = num;

    mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);

    enum Colors color = RED;

    mt_setbgcolor(color);

    sc_printCell(cell, WHITE, RED);
    printBigCharInBox(cell);
    printRedactedFormat(cell);
    printCommand(cell);
    printInOutCells();

    mt_gotoXY(26, 1);
}

int load_prog_from_file(char* path)
{
    FILE* in = fopen(path, "r");

    if (!in) {
        return 1;
    }

    for (int i = 0; i < SIZE; i++) {
        fscanf(in, "%d", &memory[i]);
    }
    fscanf(in, "%d", &accumulator);

    int value = 0;

    fscanf(in, "%d", &value);
    sc_regSet(FLAG_OVERSTEP_MEMORY, value);
    fscanf(in, "%d", &value);
    sc_regSet(FLAG_OVERFLOW_OPERATION, value);
    fscanf(in, "%d", &value);
    sc_regSet(FLAG_INVALID_COMMAND, value);
    fscanf(in, "%d", &value);
    sc_regSet(FLAG_DIVIDE_BY_ZERO, value);
    fscanf(in, "%d", &value);
    sc_regSet(FLAG_CLOCK_PULSE, value);

    fscanf(in, "%d", &icounter);

    fclose(in);

    return 0;
}

int save_prog_in_file(char* path)
{
    FILE* out = fopen(path, "w");

    for (int i = 0; i < SIZE; i++) {
        fprintf(out, "%d ", memory[i]);
    }
    fprintf(out, "%d ", accumulator);

    int value = 0;
    sc_regGet(FLAG_OVERSTEP_MEMORY, &value);
    fprintf(out, "%d ", value);
    sc_regGet(FLAG_OVERFLOW_OPERATION, &value);
    fprintf(out, "%d ", value);
    sc_regGet(FLAG_INVALID_COMMAND, &value);
    fprintf(out, "%d ", value);
    sc_regGet(FLAG_DIVIDE_BY_ZERO, &value);
    fprintf(out, "%d ", value);
    sc_regGet(FLAG_CLOCK_PULSE, &value);
    fprintf(out, "%d ", value);

    fprintf(out, "%d ", icounter);

    fflush(out);

    fclose(out);

    return 0;
}

void incrementNumStrForLogs()
{
    numStrForLogs++;
    if (numStrForLogs > 10) {
        mt_gotoXY(26, 1);
        for (int i = 26; i < 36; i++) {
            printf("                     \n");
        }
        numStrForLogs = 0;
    }
}

int interface(
        int size,
        int mem,
        int acc,
        int insCoun,
        int oper,
        int fl,
        int bc,
        int rd,
        int h,
        int ch,
        int bcib,
        int io)
{
    if (size) {
        changeSizeTerm();
    }

    if (mem) {
        if (printMemory() != 0) {
            return 1;
        }
        selectCellMemoryByNumber(cell);
    }

    if (acc) {
        if (printAccumulator() != 0) {
            return 1;
        }
    }

    if (insCoun) {
        if (printICounter() != 0) {
            return 1;
        }
    }

    if (oper) {
        if (printCommand(cell) != 0) {
            return 1;
        }
    }

    if (fl) {
        if (printFlags() != 0) {
            return 1;
        }
    }

    if (bc) {
        if (printRedacted() != 0) {
            return 1;
        }
    }

    if (rd) {
        if (printRedactedFormat(cell) != 0) {
            return 1;
        }
    }

    if (h) {
        if (printHelp() != 0) {
            return 1;
        }
    }
    if (ch) {
        if (printCache() != 0) {
            return 1;
        }
    }

    if (bcib) {
        if (printBigCharInBox(cell) != 0) {
            return 1;
        }
    }

    if (io) {
        if (printInOut() != 0) {
            return 1;
        }
        printInOutCells();
        printf("\n");
    }

    mt_gotoXY(26, 1);

    return 0;
}

int ALU(int command, int operand)
{
    if (operand > 127) {
        return 1;
    }

    switch (command) {
    case ADD: // 1e - 30 (суммирует значение аккумулятора со значением ячейки
              // по адресу операнда)
    {
        int temp = 0;
        sc_accumulatorGet(&temp);
        if ((temp + memory[operand]) >= 32767) {
            sc_regSet(FLAG_OVERFLOW_OPERATION, 1);
            break;
        }
        sc_accumulatorSet(temp + memory[operand]);
        sc_icounterSet(cell);
        break;
    }
    case SUB: // 1f - 31 (вычитает значение аккумулятора со значением ячейки по
              // адресу операнда)
    {
        if ((accumulator - memory[operand]) < -65534) {
            sc_regSet(FLAG_OVERFLOW_OPERATION, 1);
            break;
        }
        sc_accumulatorSet(accumulator - memory[operand]);
        sc_icounterSet(cell);
        break;
    }
    case DIVIDE: // 20 - 32 (делит значение аккумулятора со значением ячейки по
                 // адресу операнда)
    {
        if (memory[operand] == 0 || accumulator == 0) {
            sc_regSet(FLAG_DIVIDE_BY_ZERO, 1);
            break;
        }
        sc_accumulatorSet(accumulator / memory[operand]);
        sc_icounterSet(cell);
        break;
    }
    case MUL: // 21 - 33 (умножает значение аккумулятора со значением ячейки по
              // адресу операнда)
    {
        if ((accumulator * memory[operand]) >= 65535) {
            sc_regSet(FLAG_OVERFLOW_OPERATION, 1);
            break;
        }
        sc_accumulatorSet(accumulator * memory[operand]);
        sc_icounterSet(cell);
        break;
    }
    default:
        return 1;
    }
    fflush(stdout);
    return 0;
}

int CU() // – реализует алгоритм работы одного такта устройства управления (1
         // такт выполняется при нажатии на t)
{
    int command = 0;
    int operand = 0;
    int sign = 0;

    if (sc_commandDecode(memory[cell], &sign, &command, &operand) != 0) {
        sc_regSet(FLAG_INVALID_COMMAND, 1);
        return 1;
    }

    if (sign != 0) {
        sc_regSet(FLAG_INVALID_COMMAND, 1);
        return 1;
    }

    if (command > 33
        || command
                < 30) // команды с 30 по 33 - арифмитические, выполняются в ALU
    {
        switch (command) {
        case READ: // 0a - 10
        {
            printInOut(); // вводим в in-out
            mt_gotoXY(20 + numStrForLogs, 70);
            printf("%.2d<", operand);
            rk_readValue(&memory[operand], 1);

            printf("\n");
            if (memory[operand] > 32768) {
                sc_regSet(FLAG_OVERFLOW_OPERATION, 1);
                break;
            }
            addTerm(operand, 0);
            sc_icounterSet(cell);
            break;
        }
        case WRITE: // ob - 11 (вывод значения в in-out) (из ячейки выведится
                    // значение в in-out)
        {
            mt_gotoXY(20 + numStrForLogs, 70);
            addTerm(operand, 1);
            sc_icounterSet(cell);
            break;
        }
        case LOAD: // 14 - 20 (загрузка из ячейки на которую ссылается текущий
                   // операнд в аккумулятор)
        {
            sc_accumulatorSet(memory[operand]);
            sc_icounterSet(cell);
            break;
        }
        case STORE: // 15 - 21 (выгрузка из аккумулятора в ячейку на которую
                    // ссылается текущий операнд в аккумулятор)
        {
            sc_accumulatorGet(&memory[operand]);
            sc_icounterSet(cell);
            break;
        }
        case CHL: // 3c - 60 (логический сдвиг на 1 бит влево и загрузка
                  // обновленного значения в аккумулятор)
        {
            sc_accumulatorSet((int)memory[operand] << 1);
            sc_icounterSet(cell);
            break;
        }
        case JNP: // 3b - 59 (если предыдущая операция возвращает нечетное то
                  // перепрыгиваем на нужную ячейку)
        {
            int val;
            sc_accumulatorGet(&val);
            if (val % 2 != 0) {
                cell = operand;
            }
            cell = operand;
            sc_icounterSet(cell);
            break;
        }
        case HALT: // 2b - 43 стоп слово
            sc_icounterSet(cell);
            return 2;
            break;
        }
    } else {
        if (ALU(command, operand)) {
            return 1;
        }
    }
    return 0;
}

void test()
{
    sc_commandEncode(0, 10, 78,
                     &memory[0]); // READ 78
    sc_commandEncode(1, 10, 88,
                     &memory[1]); // READ 88

    sc_commandEncode(1, 20, 79, &memory[2]); // LOAD 79
    sc_commandEncode(1, 30, 89, &memory[3]); // ADD 89
    sc_commandEncode(1, 21, 99, &memory[4]); // STORE 99

    sc_commandEncode(1, 20, 78, &memory[5]); // LOAD 78
    sc_commandEncode(1, 30, 88, &memory[6]); // ADD 88
    sc_commandEncode(1, 21, 98, &memory[7]); // STORE 98

    sc_commandEncode(1, 43, 0, &memory[9]); // HALT

    sc_commandEncode(1, 11, 8, &memory[8]); // WRITE

    sc_commandEncode(1, 30, 109, &memory[10]); // ADD 30 to accum

    sc_commandEncode(1, 60, 0, &memory[11]); // CHL 1
    sc_commandEncode(1, 59, 2, &memory[12]); // JNP 2
}

int runtime()
{
    int statusIter = 0;

    do {
        statusIter = CU();
        mt_setdefaultcolor();
        interface(0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);

        sleep(1);
        if (statusIter == 1) {
            printf("Status Iteration = 1 ( Error )\n");
            break;
        }

        cell++;
    } while (statusIter != 2 && cell < 128); // meet halt command

    mt_gotoXY(26, 1);
    printf("End program\n");
    return 0;
}

int main()
{
    mt_clrscr();

    sc_memoryInit();
    sc_regInit();
    sc_icounterInit();
    initNumberCell();
    sc_icounterInit();
    initNumStrForLogs();
    termInit();
    // printCache1();
    enum keys key;
    enum way w;
    w = way_DEFAULT;
    selectCellMemory(w);

    test();

    interface(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
    signal(SIGALRM, sighandler);
    signal(SIGUSR1, sighandler);

    while (1) {
        rk_readkey(&key);
        if (key == ESC) {
            break;
        }
        if (key == UP) {
            w = way_UP;
            selectCellMemory(w);
            continue;
        }
        if (key == LEFT) {
            w = way_LEFT;
            selectCellMemory(w);
            continue;
        }
        if (key == DOWN) {
            w = way_DOWN;
            selectCellMemory(w);
            continue;
        }
        if (key == RIGHT) {
            w = way_RIGHT;
            selectCellMemory(w);
            continue;
        }
        if (key == F5) {
            accumulator = memory[cell];
            interface(0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            continue;
        }
        if (key == F6) {
            sc_icounterSet(cell);
            interface(0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
            continue;
        }
        if (key == ENTER) {
            printInOut();
            mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
            printf("     ");
            mt_gotoXY((cell / 10) + 2, (cell % 10) * 6 + 2);
            rk_readValue(&memory[cell], 1);
            addTerm(cell, 0);
            interface(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
            continue;
        }
        if (key == 'i') {
            sc_regInit();
            cache_clear();
            sc_memoryInit();
            cell = 0;
            interface(0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0);

            selectCellMemoryByNumber(cell);
            continue;
        }
        if (key == 'l') {
            mt_gotoXY(26 + numStrForLogs, 1);
            printf("Enter path to file: ");
            char* path = calloc(0, sizeof(char) * 30);
            scanf("%s", path);
            load_prog_from_file(path);
            // interface(0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0);
            continue;
        }
        if (key == 's') {
            mt_gotoXY(26 + numStrForLogs, 1);
            printf("Enter path to file: ");
            char* path = calloc(0, sizeof(char) * 30);
            scanf("%s", path);
            save_prog_in_file(path);
            // interface(0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0);
            continue;
        }
        if (key == 'r') {
            sc_regSet(FLAG_CLOCK_PULSE, 0);
            runtime();
            sc_regSet(FLAG_CLOCK_PULSE, 1);
            continue;
        }
        if (key == 't') {
            sc_regSet(FLAG_CLOCK_PULSE, 0);
            CU();
            sc_regSet(FLAG_CLOCK_PULSE, 1);
            fflush(stdout);
            interface(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
            continue;
        }
    }

    int rows, cols;
    mt_getscreensize(&rows, &cols);

    mt_gotoXY(rows, 1);
    return 0;
}
