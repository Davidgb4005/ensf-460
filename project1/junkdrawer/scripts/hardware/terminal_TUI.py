

import curses
def draw_layout(stdscr):
    height, width = stdscr.getmaxyx()

    left_width = width * 62 // 100
    right_width = width - left_width

    top_height = height * 50 // 100
    reserved_height = 5
    serial_height = height - top_height - reserved_height

    twin_width = left_width * 40 // 100
    diff_width = left_width - 2 * twin_width

    digital = stdscr.derwin(top_height, twin_width, 0, 0)
    difference = stdscr.derwin(top_height, diff_width, 0, twin_width)
    hardware = stdscr.derwin(top_height, twin_width, 0, twin_width + diff_width)

    digital_serial = stdscr.derwin(
        serial_height, left_width // 2,
        top_height + reserved_height, 0
    )

    hardware_serial = stdscr.derwin(
        serial_height, left_width - left_width // 2,
        top_height + reserved_height, left_width // 2
    )

    status = stdscr.derwin(height - 5, right_width, 0, left_width)

    windows = [
        digital, difference, hardware,
        digital_serial, hardware_serial, status
    ]

    for win in windows:
        win.box()
        win.refresh()

    return windows




MIN_WIDTH = 100
MIN_HEIGHT = 30

def main(stdscr):
    curses.curs_set(0)

    while True:
        height, width = stdscr.getmaxyx()
        stdscr.erase()

        if width < MIN_WIDTH or height < MIN_HEIGHT:
            msg = f"Terminal must be at least {MIN_WIDTH}x{MIN_HEIGHT}"
            if height > 0 and width > 0:
                stdscr.addnstr(0, 0, msg, width - 1)
        else:
            draw_layout(stdscr)

        stdscr.refresh()

        key = stdscr.getch()
        if key == ord('q'):
            break

curses.wrapper(main)