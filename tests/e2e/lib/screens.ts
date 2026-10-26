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
