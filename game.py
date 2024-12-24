import os
import random
import argparse
import sys

parser = argparse.ArgumentParser(description="這是一個命令列參數範例")

parser.add_argument("--num", type=int, help="大小")

args = parser.parse_args()

ROWS, COLS = args.num, args.num
NUM_MINES = 20
MINE = "*"
EMPTY = " "
HIDDEN = "■"

# Initialize board
board = [[EMPTY for _ in range(COLS)] for _ in range(ROWS)]
visible = [[False for _ in range(COLS)] for _ in range(ROWS)]

# Place mines randomly
mine_positions = random.sample(range(ROWS * COLS), NUM_MINES)
for pos in mine_positions:
    r, c = divmod(pos, COLS)
    board[r][c] = MINE

# Calculate numbers
directions = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
for r in range(ROWS):
    for c in range(COLS):
        if board[r][c] == MINE:
            continue
        count = sum(1 for dr, dc in directions if 0 <= r + dr < ROWS and 0 <= c + dc < COLS and board[r + dr][c + dc] == MINE)
        board[r][c] = str(count) if count > 0 else EMPTY

# Utility functions
def clear_screen():
    os.system("cls" if os.name == "nt" else "clear")

def render():
    clear_screen()
    print("  " + " ".join(f"{i}" for i in range(COLS)))
    for r in range(ROWS):
        row_display = f"{r} "
        for c in range(COLS):
            if visible[r][c]:
                row_display += f"{board[r][c]} "
            else:
                row_display += f"\033[1;34m{HIDDEN}\033[0m "  # Blue hidden cell
        print(row_display)

def reveal(r, c):
    if not (0 <= r < ROWS and 0 <= c < COLS) or visible[r][c]:
        return
    visible[r][c] = True
    if board[r][c] == EMPTY:
        for dr, dc in directions:
            reveal(r + dr, c + dc)

def move_cursor_to_right():
    columns = os.get_terminal_size().columns
    sys.stdout.write(f'\033[{columns}C')

# Main game loop
def main():
    while True:
        render()
        try:
            print('input: ', end='', flush=True)
            action = sys.stdin.readline().strip()
            if action == "quit":
                clear_screen()
                print("You are now in lobby")
                break            
            cmd, r, c = action.split()
            r, c = int(r), int(c)
            if cmd == "step":
                if board[r][c] == MINE:
                    print("\033[1;31mBOOM! You hit a mine!\033[0m")
                    break
                reveal(r, c)
        except (ValueError, IndexError):
            print("Invalid input. Please enter action like 'reveal 2 3'.")

        except KeyboardInterrupt:
            print("\nGame has been exited. Goodbye!")
            sys.exit(0)

if __name__ == "__main__":
    main()
