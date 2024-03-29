 jj irc
==========================================================================

A file-based IRC client.

![Image of jjp sample output](
https://raw.githubusercontent.com/aaronNGi/jj/master/jjirc.png)


 Index
--------------------------------------------------------------------------

* [Concepts](#concepts)
* [Dependencies](#dependencies)
* [Installation](#installation)
* [Usage](#usage)
* [Directory Structure](#directory-structure)
* [Input Commands](#input-commands)
* [Environment Variables](#environment-variables)
* [Log Format](#log-format)
* [Log Rotation](#log-rotation)
* [Hooks](#hooks)
* [Examples](#examples)
* [Community](#community)


 Concepts
--------------------------------------------------------------------------

jj irc is an evolution of the [ii(1)][ii homepage] IRC client. It is a
suite of multiple smaller programs which together form a full-fledged
IRC client. It consists of the following three (interchangeable) core
components:

1. `jjd` - The daemon. It does the bare minimum like connecting to the
           IRC server and reading user input from a named pipe (fifo). It
           spawns a child and forwards all user and IRC messages to it.
           Text printed by the child is directly forwarded to the IRC
           server. Written in C.
2. `jjc` - The client. Gets spawned as child of `jjd` and handles the
           more typical IRC client things. Written in AWK.
3. `jjp` - Pretty prints log files from disk or stdin. Written in AWK.

jj tries to stay true to the UNIX philosophy, which says:

> Write programs that do one thing and do it well. Write programs to work
> together. Write programs to handle text streams, because that is a
> universal interface.

The IRC output is saved in various log files, user input is read via a
named pipe and certain events are handled by external tools.

Log files *being* the IRC client makes it possible to use the systems
text mangling utilities on them (think `grep(1)`) and it automatically
adds a nice separation between the back-end (`jjq(1)`) and front-end. The
front-end can easily be stopped and restarted without affecting the
back-end. Building a front-end could be as easy as `tail -f` and some
`tmux(1)` usage. Or it could be a complex log parser, which colors
messages, right aligns nicknames and more. Want to filter out join
messages? Use `grep(1)` in the pipeline. Scrollback and search? `tmux(1)`!

Reading input via a named pipe makes it possible to script the input side
of the front-end. For instance, want to use your editor to write and send
IRC messages? No problem, you can do that!

Common tasks, e.g. auto-joining channels after connecting to an IRC
server, are not handled by `jjc(1)` itself, but instead are delegated to
external programs (usually shell scripts). That gives the user a lot of
freedom and power in terms of scriptability. To stay with the channel
auto-join example: After successfully connecting, `jjc(1)` runs
`irc_on_connect` if it's in `PATH`. That program can of course be written
in your favorite languange. It could check the current host and,
depending on server, join different channels, auth with services etc. The
`irc_on_highlight` script could be used to send desktop/push
notifications. The `irc_on_join` script could be used to automatically
open new windows in `tmux(1)`, whenever a channel is joined. See
[Hooks](#hooks) for more details.


 Dependencies
--------------------------------------------------------------------------

Nothing but a C compiler and `awk(1)`.

*Supported AWK implementations are gawk, nawk, mawk & busybox awk. The
original awk described in "The AWK Programming Language" is not working.*


 Installation
--------------------------------------------------------------------------

As root user:

```shell
make
make install
```

or as unpriviliged user:

```shell
make
make PREFIX=~/.local install
PATH=~/.local/bin:$PATH
```


 Usage
--------------------------------------------------------------------------

None of the programs have any options and instead are controlled entirely
by environment variables. To change the defaults, it makes sense to add
the desired changes to `~/.profile` or similar, so that they don't have to
be entered manually for every `jjd(1)` invocation. For the full list of
variables, see the [Environment Variables](#environment-variables)
section.

### Connecting to a Server

To connect to an IRC server, simply run:

```shell
jjd
```

Or:

```shell
IRC_HOST=irc.rizon.net jjd
```

By default `jjd(1)` connects to irc.libera.chat, using the current user as
nickname and creates the directory "irc.libera.chat" in the current
working directory. Located in that directory will be the various log files
and the named pipe for input. For more information, see [Directory
Structure](#directory-structure) and [Input Commands](#input-commands).

### Printing a Log

To save some typing, change into the directory of that new connection:

```shell
cd irc.libera.chat
```

To display the IRC output, the most basic would be to simply run:

```shell
cat server.log
```

`cat` could also be replaced with `jjp` for prettier output.

### Joining And Sending to a Channel

To join a channel and say something in it, run:

```shell
echo 'join #jj' >in
```

And then:

```shell
echo 'msg #jj Hello, World!' >in
```

The output of that channel can then be read from `channels/\#jj.log`.

Because nobody wants to type that full command for every message, a simple
loop can make this more convenient:

```shell
while IFS= read -r line; do printf 'msg #jj %s\n' "$line"; done
```

See [Examples](#examples) for a more elaborate version of that input-loop.


 Directory Structure
--------------------------------------------------------------------------

The following shows a typical channel structure tree created by jj irc.

```
irc.libera.chat/
├── channels/
│   ├── #jj.log
│   └── nickserv.log
├── in
├── motd
└── server.log
```

`server.log` is where all non-channel specific messages go. Instead of
spamming `server.log` with the servers message of the day, every time the
user connects, it is instead written to the file called `motd`. The file
named `in` is a named pipe, used for sending messages to the IRC server.
The `channels` directory contains log files of channels and private
messages.

> **Note:** The directories and the in-fifo are created by `jjd(1)`, while
>          all the logfiles are created by `jjc(1)`.


 Input Commands
--------------------------------------------------------------------------

These are the input commands supported by `jjc(1)`. All commands are
case-insensitive. "target" is a channel or a nickname. Parameters in
square brackets are optional.

| Command    | Parameter 1            | Parameter 2                | Parameter 3      | Description |
|------------|------------------------|----------------------------|------------------|-------------|
| `action`   | target                 | message                    | n/a              | Send an action message to a user or channel.
| `me`       | target                 | message                    | n/a              | An alias for `action`.
| `away`     | [away text]            | n/a                        | n/a              | Mark yourself as away. Without parameters it unsets away.
| `invite`   | nickname               | channel                    | n/a              | Invite a user to a channel.
| `join`     | channel1[,channel2]... | [password1[,password2]...] | n/a              | Join channels.
| `kick`     | channel                | nickname                   | [reason]         | Kick a user from a channel.
| `list`     | n/a                    | n/a                        | n/a              | Print a list of all currently open channels (including private message channels).
| `ls`       | n/a                    | n/a                        | n/a              | An alias for `list`.
| `message`  | target                 | [message]                  | n/a              | Send a message to a channel or user. When messaging a user, the message text can be omitted to create a private message channel.
| `msg`      | target                 | [message]                  | n/a              | An alias for `message`.
| `mode`     | target                 | [mode]                     | [mode parameter] | Set various user or channel modes. `mode #channel +b` requests the channels banlist. `mode #channel` requests the current mode.
| `names`    | channel                | n/a                        | n/a              | Request the names listing of a channel.
| `nick`     | nickname               | n/a                        | n/a              | Change your nickname.
| `notice`   | target                 | [message]                  | n/a              | Send a notice to a channel or user. The same rules as with `message` apply.
| `part`     | target                 | [reason]                   | n/a              | Leave a channel or close a private message channel.
| `quit`     | [reason]               | n/a                        | n/a              | Disconnect from the server and quit jj.
| `topic`    | channel                | n/a                        | n/a              | Request the topic of a channel.
| `topicset` | channel                | [topic]                    | n/a              | Set the topic of a channel. Omitting the second parameter removes the channel topic.
| `whois`    | nickname               | n/a                        | n/a              | Request the whois information of a user.

Additionally, the `raw` command can be used to send IRC commands not
supported by `jjc(1)`.

> **Note:** Using `raw` to message or notice a channel or user results in
>           the message not being printed locally (to the log file). It
>           could be used to auth with services to prevent passwords from
>           being written to the logs.


 Environment Variables
--------------------------------------------------------------------------

### Settings

| Name           | Description                                                                       | Default                 |
|----------------|-----------------------------------------------------------------------------------|-------------------------|
| `IRC_CLIENT`   | The program spawned as child of `jjd(1)` which handles all user and IRC messages. | `jjc`                   |
| `IRC_DIR`      | Where to store the per-host directories.                                          | `.` (current directory) |
| `IRC_HOST`     | The IRC host to connect to.                                                       | `irc.libera.chat`       |
| `IRC_NICK`     | The Nickname to use.                                                              | `$USER`                 |
| `IRC_PASSWORD` | The server password.                                                              | unset                   |
| `IRC_PORT`     | Connect using this port.                                                          | `6667`                  |
| `IRC_REALNAME` | The real name to use. Can be seen in `whois`.                                     | `$USER`                 |
| `IRC_USER`     | The user name to use.                                                             | `$USER`                 |

### Special Information for Hooks

When [Hooks](#hooks) are being run, the following additional environment
variables are also available to the called program:

| Name              | Description |
|-------------------|-------------|
| `IRC_ME`          | Our nickname.
| `IRC_NETWORK`     | The networks official name as supplied by the server. Like "QuakeNet".
| `IRC_TEXT`        | The text of a message. Or if the event is a kick, part, or quit, it would contain the reason. If applicable, empty otherwise.
| `IRC_WHERE`       | In which channel the event happened. If applicable, empty otherwise.
| `IRC_WHO`         | Who triggered this hook, e.g. the nickname of the message author. If applicable, empty otherwise.
| `IRC_CASEMAPPING` | The servers casemapping. For rfc1459 for example, its value would be `][\~A-Z }{\|^a-z`, which can be split on space and then used as arguments for `tr(1)`, to properly casefold a string.
| `IRC_AWAY`        | 1 when we are marked away, empty otherwise.

### Misc

| Name           | Description |
|----------------|-------------|
| `JJ_SERVERLOG` | When not empty, `jjp(1)` will print status messages using the default color.
| `JJ_DEBUG`     | When not emtpy, `jjc(1)` will print debug output.
| `NO_COLOR`     | Prevent `jjp(1)` from printing colors.


 Log Format
--------------------------------------------------------------------------

The general log format is:

```
1579093317 <user123>n! Hello, World!
```

The "1579093317" is the seconds since epoch. It can be converted to the
current timezone and a readable format, which `jjc(1)` does automatically.
The second part is the nickname of the message author with a suffix
indicating message types. The "Hello, World!" is the actual body of the
message.

`jjc(1)` uses the following message type indicators:

* `*` - This is us, we are the author of this message.
* `!` - Important information in server.log or our nick is mentioned in
        this message.
* `n` - This message is a notice.
* `:<status>:` - A channel message send only to users with a certain
	status in that channel (@%+ etc).
* `c` - A CTCP message.
* `a` - an ACTION message.

> **Note:** For messages without an author (server messages), a single
>           dash is used as nickname.


 Log Rotation
--------------------------------------------------------------------------

When `jjd(1)` receives a `SIGUSR1`, it will send `s <timestamp> SIGUSR1`
to `$IRC_CLIENT`. By default, `jjc(1)` will close all its open log files
when receiving that message. That way, any new messages after that will
automatically reopen the log files at their original location, completing
the log rotation process.

 Hooks
--------------------------------------------------------------------------

Certain events can trigger the execution of external programs. Those
programs have to be executable and in `PATH` and they are run with an
altered environment (See: [Environment
Variables](#environment-variables)).

The following programs are supported:

| Name              | Trigger                                         |
|-------------------|-------------------------------------------------|
| `irc_on_query`    | A private message channel is created.           |
| `irc_on_connect`  | Succesfully connecting to the server.           |
| `irc_on_ctcp`     | Receiving a CTCP message.                       |
| `irc_on_highlight`| Own nick is mentioned in a message.             |
| `irc_on_invite`   | Being invited to join a channel.                |
| `irc_on_join`     | A channel or private message channel is joined. |
| `irc_on_kick`     | We got kicked from a channel.                   |
| `irc_on_part`     | A channel is parted.                            |

> **Note:** When a private message contains our nick but also caused the
>           creation of a channel, instead of executing both,
>           `irc_on_query` *and* `irc_on_highlight`, only the former is
>           triggered. Unlike `irc_on_join`, which is always triggered,
>           `irc_on_query` is not triggered when *we* caused the channel
>           creation by messaging another user.


 Examples
--------------------------------------------------------------------------

### Securing the Connection

Using TLS via [s6-networking][s6 networking] utilities

```shell
s6-tlsclient irc.libera.chat 6697 jjd
```

Or using SSL via netcat

```shell
ncat -vv --ssl --ssl-verify -c 'exec 6<&0 7>&1; PROTO=lol jjd </dev/tty >/dev/tty' irc.libera.chat 6697
```

### Automatic Reconnection

```shell
while :; do jjd; sleep 5; done
```

### Watching Logs

```shell
tail -fn100 "$IRC_DIR/$IRC_HOST/channels/#channel.log" | jjp
```

### Ignoring Certain Nicks

```shell
tail -fn100 "$IRC_DIR/$IRC_HOST/channels/#channel.log" |
	grep -iv '^.\{10\} <nick>' | jjp
```

### Printing Last n User Messages

```shell
tac "$IRC_DIR/$IRC_HOST/channels/#channel.log" |
	grep -m10 -v '^.\{10\} <->' |
	tac | jjp
```

### A Simple Input Method

See [jji](extra/jji) in [extra](extra).

### A Sample irc_on_connect

```sh
#!/bin/sh -e

fifo=$IRC_DIR/$IRC_HOST/in
[ -p "$fifo" ] && [ -w "$fifo" ] ||
	exit 1

if [ "$IRC_HOST" = irc.libera.chat ]; then
	printf 'raw PRIVMSG NickServ :IDENTIFY jilles foo\n' >"$fifo"
	sleep .5
	printf 'join #jj\n' >"$fifo"
fi
```


 Community
--------------------------------------------------------------------------

### Contributions/Scripts

* [panica][panica github] - Thin tmux wrapper around jj

### IRC

Join `#jj` on irc.libera.chat

[ii homepage]: https://tools.suckless.org/ii/
[s6 networking]: https://skarnet.org/software/s6-networking/
[panica github]:  https://github.com/guysv/panica
