/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GS_UART_BACKEND_STDIO_H_
#define _GS_UART_BACKEND_STDIO_H_

#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <unistd.h>

#include <async_event.h>
#include <uutils.h>
#include <ports/biflow-socket.h>
#include <module_factory_registery.h>
#include <queue>
#include <signal.h>
#include <regex>
#include <atomic>
#include <cstdlib>

// TODO convert scp warn to scp fatal

#ifdef _WIN32
#include <windows.h>
typedef HANDLE descriptor_t;
typedef DWORD console_mode_t;
#else
#include <termios.h>
#include <poll.h>
typedef int descriptor_t;
typedef struct termios console_mode_t;
#endif // _WIN32

#define RCV_POLL_TIMEOUT_MS 300

class char_backend_stdio : public sc_core::sc_module
{
protected:
    cci::cci_param<bool> p_read_write;
    cci::cci_param<std::string> p_expect;
    cci::cci_param<std::string> p_highlight;

private:
    std::atomic_bool m_running;
    std::thread rcv_thread_id;
    std::atomic<bool> c_flag;
    SCP_LOGGER();
    std::string line;
    std::string ecmd;
    sc_core::sc_event ecmdev;
    bool processing = false;
    static descriptor_t stdin_fd;

#ifdef _WIN32
    const static size_t input_buffer_size = 128;
    INPUT_RECORD irInBuf[input_buffer_size];
#endif

public:
    gs::biflow_socket<char_backend_stdio> socket;
    static console_mode_t old_console_mode;
    static bool old_console_mode_valid;

private:
#ifdef _WIN32
    static void restore_old_mode()
    {
        BOOL res = SetConsoleMode(stdin_fd, old_console_mode);
        if (!res) {
            set_default_mode();
        }
    }

    static void set_raw_mode()
    {
        if (!old_console_mode_valid && GetConsoleMode(stdin_fd, &old_console_mode)) {
            old_console_mode_valid = true;
        }

        BOOL res = SetConsoleMode(stdin_fd, ENABLE_PROCESSED_INPUT);
    }

    static void set_default_mode()
    {
        SetConsoleMode(stdin_fd, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    }

    descriptor_t get_stdin_descriptor()
    {
        descriptor_t stdin_file_descriptor = GetStdHandle(STD_INPUT_HANDLE);
        if (stdin_file_descriptor == INVALID_HANDLE_VALUE) {
            SCP_WARN(())("char_backend_stdio: Failed to get standard input descriptor. Error: {}", GetLastError());
        }

        return stdin_file_descriptor;
    }

    void enqueue_multiple(const INPUT_RECORD irInBuf[], const DWORD cNumRead)
    {
        for (DWORD i = 0; i < cNumRead; ++i) {
            const INPUT_RECORD& record = irInBuf[i];
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
                if (record.Event.KeyEvent.uChar.AsciiChar == 0x1c) {
                    raise(SIGBREAK);
                    return;
                }
                enqueue(record.Event.KeyEvent.uChar.AsciiChar);
            }
        }
    }

    void rcv_thread()
    {
        DWORD cNumRead = 0;

        while (m_running) {
            DWORD waitResult = WaitForSingleObject(stdin_fd, RCV_POLL_TIMEOUT_MS);

            if (waitResult == WAIT_TIMEOUT) {
                if (c_flag) {
                    c_flag = false;
                    enqueue('\x03');
                }
                continue;
            } else if (waitResult == WAIT_OBJECT_0) {
                if (ReadConsoleInput(stdin_fd, irInBuf, input_buffer_size, &cNumRead)) {
                    enqueue_multiple(irInBuf, cNumRead);
                } else {
                    SCP_WARN(())("char_backend_stdio: Failed to read console input. Error: {}", GetLastError());
                }
            } else {
                break;
            }
        }
    }
#else
    descriptor_t get_stdin_descriptor() { return STDIN_FILENO; } /* stdin */

    static void restore_old_mode()
    {
        if (tcsetattr(stdin_fd, TCSANOW, &old_console_mode) == -1) {
            set_default_mode();
        }
    }

    static void set_raw_mode()
    {
        console_mode_t tty;
        tcgetattr(stdin_fd, &tty);
        if (!old_console_mode_valid) {
            old_console_mode = tty;
            old_console_mode_valid = true;
        }
        tty.c_lflag &= ~(ECHO | ICANON | IEXTEN);
        tcsetattr(stdin_fd, TCSANOW, &tty);
    }

    static void set_default_mode()
    {
        console_mode_t tty;
        tcgetattr(stdin_fd, &tty);
        tty.c_lflag |= ECHO | ICANON | IEXTEN;
        tcsetattr(stdin_fd, TCSANOW, &tty);
    }

    void rcv_thread()
    {
        int fd = STDIN_FILENO;
        struct pollfd rcv_monitor;
        rcv_monitor.fd = fd;
        rcv_monitor.events = POLLIN;

        while (m_running) {
            int ret = poll(&rcv_monitor, 1, RCV_POLL_TIMEOUT_MS);
            if ((ret == -1 && errno == EINTR) || (ret == 0) /*timeout*/) {
                if (c_flag) {
                    c_flag = false;
                    enqueue('\x03');
                }
                continue;
            } else if (rcv_monitor.revents & POLLIN) {
                char c;
                int r = read(fd, &c, 1);
                if (r == 1) {
                    enqueue(c);
                }
                if (r == 0) {
                    break;
                }
            } else {
                break;
            }
        }
    }
#endif

public:
    void catch_fn(int signo) {}

    static void tty_reset()
    {
        if (old_console_mode_valid) {
            restore_old_mode();
        } else {
            set_default_mode();
        }
    }

    // trim from end of string (right)
    std::string& rtrim(std::string& s)
    {
        const char* t = "\n\r\f\v";
        s.erase(s.find_last_not_of(t) + 1);
        return s;
    }
    std::string getecmdstr(const std::string& cmd)
    {
        return ecmd.substr(cmd.length(), ecmd.find_first_of("\n") - cmd.length());
    }
    void process()
    {
        processing = true;
        expect_process();
    }
    void expect_process()
    {
        if (!processing) return;
        const std::string expectstr = "expect ";
        const std::string sendstr = "send ";
        const std::string waitstr = "wait ";
        const std::string exitstr = "exit";
        do {
            if (ecmd.substr(0, expectstr.length()) == expectstr) {
                std::string str = getecmdstr(expectstr);
                std::regex restr(str);
                if (std::regex_search(line, restr)) {
                    SCP_WARN(())("Found expect string {}", rtrim(line));
                    ecmd.erase(0, ecmd.find_first_of("\n"));
                    if (ecmd != "") ecmd.erase(0, 1);
                    if (ecmd.substr(0, expectstr.length()) == expectstr) break;
                    continue;
                }
            }
            if (ecmd.substr(0, sendstr.length()) == sendstr) {
                SCP_WARN(())("Sending expect string {}", getecmdstr(sendstr));
                for (char c : getecmdstr(sendstr)) {
                    enqueue(c);
                }
                enqueue('\n');
                ecmd.erase(0, ecmd.find_first_of("\n"));
                if (ecmd != "") ecmd.erase(0, 1);
                continue;
            }
            if (ecmd.substr(0, waitstr.length()) == waitstr) {
                SCP_WARN(())("Expect waiting {}s", getecmdstr(waitstr));
                processing = false;
                ecmdev.notify(stod(getecmdstr(waitstr)), sc_core::SC_SEC);
                ecmd.erase(0, ecmd.find_first_of("\n"));
                if (ecmd != "") ecmd.erase(0, 1);
                break;
            }
            if (ecmd.substr(0, exitstr.length()) == exitstr) {
                SCP_WARN(())("Expect caused exit");
                sc_core::sc_stop();
            }
            break;
        } while (true);
    }

    /*
     * char_backend_stdio constructor
     * Parameters:
     *   \param read_write bool : when set to true, accept input from STDIO
     *   \param expect String : An 'expect' like string of commands, each command should be separated by \n
     *  The acceptable commands are:
     *      expect [string] : dont process any more commands until the "string" is seen on the output (STDIO)
     *      send [string]   : send "string" to the input buffer (NB this will happen whether of not read_write is set
     *      wait [float]    : Wait for "float" (simulated) seconds, until processing continues.
     *      exit            : Cause the simulation to terminate normally.
     */
    char_backend_stdio(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_read_write("read_write", true, "read_write if true start rcv_thread")
        , p_expect("expect", "", "string of expect commands")
        , p_highlight("ansi_highlight", "", "ANSI highlight code to use for output, default bold")
        , socket("biflow_socket")
        , m_running(true)
        , c_flag(false)
    {
        SCP_TRACE(()) << "CharBackendStdio constructor";

        stdin_fd = get_stdin_descriptor();

        ecmd = p_expect;
        SC_METHOD(process);
        sensitive << ecmdev;
        if (p_expect.get_value() != "") {
            ecmd = p_expect;
            processing = true;
            SCP_WARN(())("Processing expect string {}", ecmd);
        }

        gs::SigHandler::get().register_on_exit_cb(std::string(this->name()) + ".char_backend_stdio::tty_reset",
                                                  tty_reset);
        gs::SigHandler::get().add_sigint_handler(gs::Handler_CB::PASS);
        gs::SigHandler::get().register_handler(std::string(this->name()) + ".char_backend_stdio::SIGINT_handler",
                                               [&](int signo) {
                                                   if (signo == SIGINT) {
                                                       c_flag = true;
                                                   }
                                               });
        if (p_read_write) rcv_thread_id = std::thread(&char_backend_stdio::rcv_thread, this);

        socket.register_b_transport(this, &char_backend_stdio::writefn);
        std::atexit(tty_reset);
    }

    void end_of_elaboration() { socket.can_receive_any(); }

    void enqueue(char c) { socket.enqueue(c); }

    void writefn(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
    {
        uint8_t* data = txn.get_data_ptr();
        if (!p_highlight.get_value().empty()) std::cout << p_highlight.get_value();
        for (int i = 0; i < txn.get_streaming_width(); i++) {
            putchar(data[i]);
            if ((char)data[i] == '\n') {
                expect_process();
                line = "";
            } else {
                line = line + (char)data[i];
            }
        }
        if (!p_highlight.get_value().empty()) std::cout << "\x1B[0m"; // ANSI color reset.
        fflush(stdout);
    }

    void start_of_simulation()
    {
        if (!old_console_mode_valid) {
            set_raw_mode();
        }
    }

    void end_of_simulation() { tty_reset(); }

    ~char_backend_stdio()
    {
        gs::SigHandler::get().deregister_on_exit_cb(std::string(name()) + ".char_backend_stdio::tty_reset");
        gs::SigHandler::get().deregister_handler(std::string(name()) + ".char_backend_stdio::SIGINT_handler");
        m_running = false;
        if (rcv_thread_id.joinable()) rcv_thread_id.join();
        tty_reset();
    }
};
extern "C" void module_register();
#endif
