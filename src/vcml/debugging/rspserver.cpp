/******************************************************************************
 *                                                                            *
 * Copyright 2018 Jan Henrik Weinstock                                        *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *     http://www.apache.org/licenses/LICENSE-2.0                             *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 *                                                                            *
 ******************************************************************************/

#include "vcml/logging/logger.h"
#include "vcml/debugging/rspserver.h"

namespace vcml { namespace debugging {

    static inline string opcode(const string& s) {
        size_t pos = s.find(',');
        if (pos == std::string::npos)
            return s;
        return s.substr(0, pos);
    }

    static inline int checksum(const char* str) {
        int result = 0;
        while (str && *str)
            result += static_cast<int>(*str++);
        return result & 0xff;
    }

    static inline int char2int(char c) {
        return ((c >= 'a') && (c <= 'f')) ? c - 'a' + 10 :
               ((c >= 'A') && (c <= 'F')) ? c - 'A' + 10 :
               ((c >= '0') && (c <= '9')) ? c - '0' :
               (c == '\0') ? 0 : -1;
    }

    static inline char int2char(int h) {
        static const char hexchars[] =
            {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
        return hexchars[h & 0xf];
    }

    rspserver::rspserver(u16 port):
        m_echo(false),
        m_port(),
        m_sock(port),
        m_running(false),
        m_thread() {
        m_port = m_sock.port();
    }

    rspserver::~rspserver() {
        m_running = false;

        disconnect();
        m_sock.unlisten();

        if (m_thread.joinable())
            m_thread.join();
    }

    void rspserver::send_packet(const char* format, ...) {
        va_list args;
        va_start(args, format);
        send_packet(vmkstr(format, args));
        va_end(args);
    }

    void rspserver::send_packet(const string& s) {
        VCML_ERROR_ON(!is_connected(), "no connection established");

        string esc = escape(s, "$#");
        int sum = checksum(esc.c_str());
        stringstream ss;
        ss << "$" << esc << "#"
           << int2char((sum >> 4) & 0xf)
           << int2char((sum >> 0) & 0xf);

        char ack;
        int attempts = 10;

        do {
            if (attempts-- == 0) {
                log_error("giving up sending packet");
                disconnect();
                return;
            }

            if (m_echo)
                log_debug("sending packet '%s'", ss.str().c_str());

            m_sock.send(ss.str());
            ack = m_sock.recv_char();

            if (m_echo)
                log_debug("received ack '%c'", ack);
        } while (ack != '+');
    }

    string rspserver::recv_packet() {
        VCML_ERROR_ON(!is_connected(), "no connection established");
        unsigned int checksum = 0;
        stringstream ss;
        while (true) {
            char ch = m_sock.recv_char();
            switch (ch) {
            case '$':
                checksum = 0;
                ss.str("");
                break;

            case '#': {
                if (m_echo)
                    log_debug("received packet '%s'", ss.str().c_str());

                unsigned int refsum = 0;
                refsum |= char2int(m_sock.recv_char()) << 4;
                refsum |= char2int(m_sock.recv_char()) << 0;

                if (refsum != checksum) {
                    log_debug("checksum mismatch %d != %d", refsum, checksum);
                    m_sock.send_char('-');
                    checksum = 0;
                    ss.str("");
                    break;
                }

                if (m_echo)
                    log_debug("sending ack '+'");

                m_sock.send_char('+');
                return ss.str();
            }

            case '\\':
                checksum = (checksum + ch) & 0xff;
                ch = m_sock.recv_char();
                // no break

            default:
                checksum = (checksum + ch) & 0xff;
                ss << ch;
                break;
            }
        }

        VCML_ERROR("error receiving rsp packet");
        return "";
    }

    int rspserver::recv_signal(time_t timeoutms) {
        if (!is_connected())
            return 0;
        if (!m_sock.peek(timeoutms))
            return 0;
        return m_sock.recv_char();
    }

    void rspserver::listen() {
        m_sock.listen(m_port);
        m_sock.accept();
        m_sock.unlisten();
        handle_connect(m_sock.peer());
    }

    void rspserver::disconnect() {
        if (m_sock.is_connected()) {
            m_sock.disconnect();
            handle_disconnect();
        }
    }

    void rspserver::run_async() {
        m_thread = thread(std::bind(&rspserver::run, this));
        stringstream ss; ss << "rsp_" << m_port;
        set_thread_name(m_thread, ss.str());
    }

    void rspserver::run() {
        m_running = true;
        while (m_running) try {
            disconnect();
            listen();
            while (m_running && is_connected()) try {
                string command = recv_packet();
                string response = handle_command(command);
                if (is_connected())
                    send_packet(response);
            } catch (vcml::report& r) {
                log_debug("%s", r.message()); // not an error, e.g. disconnect
                break;
            }
        } catch (vcml::report& r) {
            logger::log(r);
            return;
        }
    }

    void rspserver::stop() {
        VCML_ERROR_ON(!m_running, "server not running");
        m_running = false;
    }

    string rspserver::handle_command(const string& command) {
        try {
            string op = opcode(command);
            if (!stl_contains(m_handlers, op))
                return ""; // empty response means command not supported
            return m_handlers[op](command.c_str());
        } catch (report& rep) {
            logger::log(rep);
            return ERR_INTERNAL;
        } catch (std::exception& ex) {
            log_warn("%s", ex.what());
            return ERR_INTERNAL;
        }
    }

    void rspserver::handle_connect(const char* peer) {
        // to be overloaded
    }

    void rspserver::handle_disconnect() {
        // to be overloaded
    }

    void rspserver::register_handler(const char* cmd, handler h) {
        m_handlers[cmd] = h;
    }

    void rspserver::unregister_handler(const char* cmd) {
        m_handlers.erase(cmd);
    }

    const char* rspserver::ERR_COMMAND  = "E01";
    const char* rspserver::ERR_PARAM    = "E02";
    const char* rspserver::ERR_INTERNAL = "E03";
    const char* rspserver::ERR_UNKNOWN  = "E04";
    const char* rspserver::ERR_PROTOCOL = "E05";

}}
