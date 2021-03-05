/**
 *
 * A SUPA quick tutorial on how to use this file in case you're wondering how we control wifi
 *
 * First you call "wpa_ctrl_open" with the string "/var/run/wpa_supplicant/(one_of_the_interfaces_in_there)"
 *
 * You should let the user choose which interface inside that folder "/var/run/wpa_supplicant/" they would like
 * to be talking to, for instance they might have multiple wifi cards or something and want to use a specific one.
 * (or do some fancy stuff (like we probably do) to choose which to show by default to the user)
 *
 * The next important function is:
 *
 * int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
 *                   char *reply, size_t *reply_len,
 *                   void (*msg_cb)(char *msg, size_t len));
 *
 * This function lets us make requests to the interface and receive replies.
 * The first argument is the "ctrl" struct returned by "wpa_ctrl_open"
 * The second argument "cmd" is a string telling the interface what you want it to do. (Talked more about later)
 * The third argument "cmd_len" is the length of that string
 * The fourth argument "reply" is a "char buffer[SIZE]" that you should pass the function by pointer so it'll fill it with the response of your request
 * The fifth argument "reply_len" is the length of that reply buffer
 * The sixth and optional argument is a callback to receive messages that happened on the interface while the request was being sent (I'll talk about this a little more later)
 *
 * Like I said before, that callback function gets called if there was a message received by the interface while we sent our message,
 * but I recommend you always pass NULL because what you should really do to handle messages from the interface is the following:
 *
 * To watch for messages on the interface use the following function:
 *
 * int wpa_ctrl_attach(struct wpa_ctrl *ctrl);
 *
 * That function will tell the interface that you would like to receive messages like CONNECTED, DISCONNECT, SCAN_FINISHED,
 * or whatever else the interface reports.
 *
 * Then to actually receive those messages you would do something like:
 *
 * while (wpa_ctrl_recv(ctrl, reply, reply_len) == 0) {
 *
 * }
 *
 * The problem with the above code is that wpa_ctrl_recv is blocking, so it's going to wait until it receives an event,
 * and we can't have our entire program be blocked from progressing because of this so;
 * To make it non-blocking you can do something like:
 *
 * while (wpa_ctrl_pending(ctrl)) {
 *     if (wpa_ctrl_recv(ctrl, reply, reply_len) == 0) {
 *
 *     }
 * }
 *
 * That code checks if there is a message pending and then reads it. You'll just have to call that every so often
 * (like every 200ms (I don't know! that's just a completely made up number with no significance)) so you handle messages.
 *
 * A little more advanced solution is, if at the heart of your program is an epoll loop or whatever (like our program "winbar" is)
 * then you should use the function:
 *
 * int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl);
 *
 * That'll return the file descriptor and you can add it to your epoll to be notified when a new message is available.
 *
 * MOST IMPORTANT PART:
 * That callback argument to "wpa_ctrl_request" I said I was going to talk about. Well, now is the time.
 * For every interface, you should create two ctrl structures using the "wpa_ctrl_open" function.
 * One of them you'll use only to send messages, and the other you'll use to read and be aware of messages that can't be replied to instantly.
 * That's why I recommend you just pass null as the callback, since those messages should be handled by the
 * "message listener ctrl" instead of the "message sender ctrl".
 *
 * ALSO:
 * You may be asking yourself, what are the messages you can send to wpa_ctrl_request?
 * Well as of the time of writing this, the only message I know and tested is the string PING, which upon success returns PONG from the interface.
 * You can just look at the rest of the "winbar" code to see what messages we send.
 * AKA find all the calls we make to "wpa_ctrl_request" with grep or something :smiley_face:
 *
 */

#ifndef WPA_CTRL_H
#define WPA_CTRL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include "stddef.h"

/**
 * wpa_ctrl_open - Open a control interface to wpa_supplicant/hostapd
 * @ctrl_path: Path for UNIX domain sockets; ignored if UDP sockets are used.
 * Returns: Pointer to abstract control interface data or %NULL on failure
 *
 * This function is used to open a control interface to wpa_supplicant/hostapd.
 * ctrl_path is usually /var/run/wpa_supplicant or /var/run/hostapd. This path
 * is configured in wpa_supplicant/hostapd and other programs using the control
 * interface need to use matching path configuration.
 */
struct wpa_ctrl *wpa_ctrl_open(const char *ctrl_path);


/**
 * wpa_ctrl_close - Close a control interface to wpa_supplicant/hostapd
 * @ctrl: Control interface data from wpa_ctrl_open()
 *
 * This function is used to close a control interface.
 */
void wpa_ctrl_close(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_request - Send a command to wpa_supplicant/hostapd
 * @ctrl: Control interface data from wpa_ctrl_open()
 * @cmd: Command; usually, ASCII text, e.g., "PING"
 * @cmd_len: Length of the cmd in bytes
 * @reply: Buffer for the response
 * @reply_len: Reply buffer length
 * @msg_cb: Callback function for unsolicited messages or %NULL if not used
 * Returns: 0 on success, -1 on error (send or receive failed), -2 on timeout
 *
 * This function is used to send commands to wpa_supplicant/hostapd. Received
 * response will be written to reply and reply_len is set to the actual length
 * of the reply. This function will block for up to two seconds while waiting
 * for the reply. If unsolicited messages are received, the blocking time may
 * be longer.
 *
 * msg_cb can be used to register a callback function that will be called for
 * unsolicited messages received while waiting for the command response. These
 * messages may be received if wpa_ctrl_request() is called at the same time as
 * wpa_supplicant/hostapd is sending such a message. This can happen only if
 * the program has used wpa_ctrl_attach() to register itself as a monitor for
 * event messages. Alternatively to msg_cb, programs can register two control
 * interface connections and use one of them for commands and the other one for
 * receiving event messages, in other words, call wpa_ctrl_attach() only for
 * the control interface connection that will be used for event messages.
 */
int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
                     char *reply, size_t *reply_len,
                     void (*msg_cb)(char *msg, size_t len));


/**
 * wpa_ctrl_attach - Register as an event monitor for the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 0 on success, -1 on failure, -2 on timeout
 *
 * This function registers the control interface connection as a monitor for
 * wpa_supplicant/hostapd events. After a success wpa_ctrl_attach() call, the
 * control interface connection starts receiving event messages that can be
 * read with wpa_ctrl_recv().
 */
int wpa_ctrl_attach(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_detach - Unregister event monitor from the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 0 on success, -1 on failure, -2 on timeout
 *
 * This function unregisters the control interface connection as a monitor for
 * wpa_supplicant/hostapd events, i.e., cancels the registration done with
 * wpa_ctrl_attach().
 */
int wpa_ctrl_detach(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_recv - Receive a pending control interface message
 * @ctrl: Control interface data from wpa_ctrl_open()
 * @reply: Buffer for the message data
 * @reply_len: Length of the reply buffer
 * Returns: 0 on success, -1 on failure
 *
 * This function will receive a pending control interface message. This
 * function will block if no messages are available. The received response will
 * be written to reply and reply_len is set to the actual length of the reply.
 * wpa_ctrl_recv() is only used for event messages, i.e., wpa_ctrl_attach()
 * must have been used to register the control interface as an event monitor.
 */
int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len);


/**
 * wpa_ctrl_pending - Check whether there are pending event messages
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: 1 if there are pending messages, 0 if no, or -1 on error
 *
 * This function will check whether there are any pending control interface
 * message available to be received with wpa_ctrl_recv(). wpa_ctrl_pending() is
 * only used for event messages, i.e., wpa_ctrl_attach() must have been used to
 * register the control interface as an event monitor.
 */
int wpa_ctrl_pending(struct wpa_ctrl *ctrl);


/**
 * wpa_ctrl_get_fd - Get file descriptor used by the control interface
 * @ctrl: Control interface data from wpa_ctrl_open()
 * Returns: File descriptor used for the connection
 *
 * This function can be used to get the file descriptor that is used for the
 * control interface connection. The returned value can be used, e.g., with
 * select() while waiting for multiple events.
 *
 * The returned file descriptor must not be used directly for sending or
 * receiving packets; instead, the library functions wpa_ctrl_request() and
 * wpa_ctrl_recv() must be used for this.
 */
int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl);

#ifdef  __cplusplus
}
#endif

#endif /* WPA_CTRL_H */
