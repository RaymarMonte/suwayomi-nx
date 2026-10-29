// Where things are on the app's screens (WP-08), in screen pixels (1280x720), for touch. Read from the desktop screenshots;
// the Switch draws the same layout. Rows of cells and tabs are 72 px apart (D16: 72 px touch targets).

/** The main screen's tabs, top to bottom (MainModel::tabs()). */
export const TABS = ['Library', 'Updates', 'History', 'Browse', 'Settings'] as const;
export function tab(name: (typeof TABS)[number]): { x: number; y: number } {
    return { x: 225, y: 156 + 72 * TABS.indexOf(name) };
}

/** Settings tab rows (ui_login shows "Log out" as the third row; the other modes don't have it). */
export function settingsRow(row: 'address' | 'login' | 'logout' | 'serverVersion', uiLogin: boolean): { x: number; y: number } {
    const order = uiLogin ? ['address', 'login', 'logout', 'serverVersion'] : ['address', 'login', 'serverVersion'];
    return { x: 845, y: 206 + 72 * order.indexOf(row) };
}

/** The setup screen without username/password rows (login "none"). */
export const SETUP = { url: { x: 640, y: 184 }, test: { x: 369, y: 352 }, continue: { x: 941, y: 352 } };

/** The login screen: username, password, then the buttons. */
export const LOGIN = { login: { x: 369, y: 352 } };

/** The footer's clock (bottom left). On the Switch the same corner has battery and Wi-Fi. */
export const CLOCK_MASK = { x: 0, y: 650, w: 420, h: 70, why: 'clock, battery, Wi-Fi' };

/**
 * Desktop only: the "OK" hint of borealis's text dialog (the keyboard on the PC). A tap on it submits. Enter doesn't work
 * there: while the dialog is open, GLFW hands keys to the input method, which swallows Return.
 */
export const KEYBOARD_OK = { x: 937, y: 218 };

/**
 * The Library tab (WP-09): the category bar (y 123: the test server's categories, then Sort and Refresh) and the cover grid
 * (6 columns of 120 x 232 px cells, 16 px apart; with the grid at the top the first cover's top-left corner is 445,184).
 */
export const LIBRARY = {
    categories: {
        default: { x: 481, y: 123 },
        bigShelf: { x: 834, y: 123 },
        empty: { x: 936, y: 123 },
    },
    sort: { x: 1090, y: 123 },
    refresh: { x: 1211, y: 123 },
    /** The sort menu (borealis dropdown): Title, Unread, Last read. */
    sortMenu: { unread: { x: 640, y: 516 } },
    /** The centre of cover `index`'s image, while the grid is scrolled to the top (first two rows). */
    cover(index: number): { x: number; y: number } {
        return { x: 445 + 60 + 136 * (index % 6), y: 184 + 90 + 248 * Math.floor(index / 6) };
    },
    /** A flick up over the grid scrolls down (and the other way). How far one goes depends on its speed, so scenarios
     * flick until the grid says it got there, up to maxFlicks times. */
    flickFrom: { x: 845, y: 600 },
    flickTo: { x: 845, y: 220 },
    maxFlicks: 40,
};

/** The Library tab's other category tabs (the test server's order: Default, Reading, Long series, Big shelf, Empty). */
export const LIBRARY_MORE = {
    reading: { x: 587, y: 123 },
    longSeries: { x: 708, y: 123 },
};

/**
 * The manga details screen (WP-10). Left: Continue and the description. Right: the bar (Sort, Show, Refresh) and the
 * chapter list (rows of 800 x 80 px, 4 px apart; with the list at the top the first row's top is at y 167).
 */
export const MANGA = {
    continue: { x: 228, y: 343 },
    description: { x: 228, y: 450 },
    sort: { x: 942, y: 123 },
    filter: { x: 1100, y: 123 },
    refresh: { x: 1211, y: 123 },
    /** The middle of row `index` (a tap opens the chapter), while the list is scrolled to the top. */
    row(index: number): { x: number; y: number } {
        return { x: 760, y: 207 + 84 * index };
    },
    /** The "..." at the end of row `index` (a tap opens the chapter's menu). */
    rowMenu(index: number): { x: number; y: number } {
        return { x: 1224, y: 207 + 84 * index };
    },
    /** Entry `index` of a borealis dropdown with `count` entries (they sit at the bottom of the screen, 72 px apart). */
    menuEntry(index: number, count: number): { x: number; y: number } {
        return { x: 640, y: 588 - 72 * (count - 1 - index) };
    },
};
