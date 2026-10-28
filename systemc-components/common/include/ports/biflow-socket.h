/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @brief Bi-directional Flow controlled socket.
 *
 * The bi-directional flow controlled socket is intended to be used between devices and serial-like
 * back-ends.
 *
 * The socket transports 'standard' Generic Protocol TLM packets, but _ONLY_ makes use of the data
 * component. The socket is templated on a type (T) which is the unit of data that will be
 * transmitted. (Default is uint8)
 *
 * The interface allows the socket owner to allow a specific, or unlimited amount of data to arrive.
 * Hence the owner may specify that only one item can arrive (hence use a send/acknowledge
 * protocol), or it may use a limited buffer protocol (typical of a uart for instance) or, for
 * instance, for an output to a host, which itself is normally buffered, the socket may be set to
 * receive unlimited data.
 *
 * # **** NOTICE : async_detach_suspending / async_attach_suspending ****
 * The bidirectional socket will own an async_event, 'waiting' for the other side of the
 * communication.
 * - In the case of ZERO, _or_ UNLIMITED data, the expectation is that the async_event should _NOT_
 * cause systemC to wait for external events
 * - In the case of an absolute value above 0, the expectation is that SystemC _SHOULD_ wait for
 * external events.
 *
 * NOTE: Hence, all sockets start DETACHED, and will only be attached if/when a non-zero absolute
 * value is received from the other side.
 */

#ifndef GS_BIFLOW_SOCKET_H
#define GS_BIFLOW_SOCKET_H
#include <systemc>
#include <cci_configuration>
#include <scp/report.h>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_sockets_buswidth.h>
#include <async_event.h>
#include <memory>

namespace gs {

/*
 * Binding is asymmetrical in nature, one side called bind, the other side is 'bound to' using the get_....
 * interfaces. The result is a symmetrical complete bind.
 * To control the binding process, before the get_... interfaces are called, and sockets bound, new_bind must be called,
 * and on completion bind_done must be called.
 */
struct biflow_bindable {
    virtual void bind(biflow_bindable& other) = 0;

    virtual void new_bind(biflow_bindable& other) = 0;

    virtual tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_input_socket() = 0;
    virtual tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_output_socket() = 0;
    virtual tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_input_control_socket() = 0;
    virtual tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_output_control_socket() = 0;
    virtual void bind_done(void) = 0;

    virtual const char* name(void) = 0;
};
struct biflow_multibindable : biflow_bindable {
};

template <class MODULE, class T = uint8_t>
class biflow_socket : public sc_core::sc_module, public biflow_bindable
{
    SCP_LOGGER();

    uint32_t m_can_send = 0;
    bool m_infinite = false;
    std::vector<T> m_queue;
    gs::async_event m_send_event;
    std::mutex m_mutex;
    std::unique_ptr<tlm::tlm_generic_payload> m_txn;

    struct ctrl {
        enum { DELTA_CHANGE, ABSOLUTE_VALUE, INFINITE_VALUE } cmd;
        uint32_t can_send;
    };

    void sendall()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        tlm::tlm_generic_payload txn;

        uint64_t sending = (m_infinite || (m_can_send > m_queue.size())) ? m_queue.size() : m_can_send;
        if (sending > 0) {
            if (m_txn)
                txn.deep_copy_from(*m_txn);
            else
                txn.set_data_length(sending);
            txn.set_streaming_width(sending);
            txn.set_data_ptr(&(m_queue[0]));
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            output_socket->b_transport(txn, delay);
            m_queue.erase(m_queue.begin(), m_queue.begin() + sending);
            m_can_send -= sending;
        }
    }
    void initiator_ctrl(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        sc_assert(txn.get_data_length() == sizeof(ctrl));
        ctrl* c = (ctrl*)(txn.get_data_ptr());
        switch (c->cmd) {
        case ctrl::ABSOLUTE_VALUE:
            m_can_send = c->can_send;
            m_infinite = false;
            SCP_TRACE(())("Can send {}", m_can_send);
            // If the other side signals a specific number, then it must be waiting on interrupts
            // etc to drive this, so lets interpret that as a 'signal' that we should keep the
            // suspending flag. If this is set to 0, then the other side stopped listening
            if (m_can_send)
                m_send_event.async_attach_suspending();
            else
                m_send_event.async_detach_suspending();
            break;
        case ctrl::DELTA_CHANGE:
            sc_assert(m_infinite == false);
            m_can_send += c->can_send;
            m_infinite = false;
            SCP_TRACE(())("Can send {}", m_can_send);
            break;
        case ctrl::INFINITE_VALUE:
            m_infinite = true;
            SCP_TRACE(())("Can send any");
            break;
        default:
            SCP_FATAL(())("Unkown command");
        }
        m_send_event.notify(sc_core::SC_ZERO_TIME);
    }
    void send_ctrl(ctrl& c)
    {
        tlm::tlm_generic_payload txn;
        txn.set_data_length(sizeof(ctrl));
        txn.set_data_ptr(((uint8_t*)&c));
        txn.set_command(tlm::TLM_IGNORE_COMMAND);
        sc_core::sc_time t;
        input_control_socket->b_transport(txn, t);
    }

    bool m_bound = false;

public:
    void new_bind(biflow_bindable& other)
    {
        if (m_bound) SCP_ERR(())("Socket already bound, may only be bound once");
    }
    void bind_done(void)
    {
        if (m_bound) SCP_ERR(())("Socket already bound, may only be bound once");
        m_bound = true;
    }

    bool is_bound() const { return m_bound; }

    const char* name() { return sc_module::name(); }

    tlm_utils::simple_target_socket<MODULE, DEFAULT_TLM_BUSWIDTH> input_socket;
    tlm_utils::simple_initiator_socket<biflow_socket, DEFAULT_TLM_BUSWIDTH> input_control_socket;

    tlm_utils::simple_initiator_socket<biflow_socket, DEFAULT_TLM_BUSWIDTH> output_socket;
    tlm_utils::simple_target_socket<biflow_socket, DEFAULT_TLM_BUSWIDTH> output_control_socket;

    /**
     * @brief Construct a new biflow socket object
     *
     * @param name
     */
    biflow_socket(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , input_socket((std::string(name) + "_input_socket").c_str())
        , output_socket((std::string(name) + "_output_socket").c_str())
        , input_control_socket((std::string(name) + "_input_socket_control").c_str())
        , output_control_socket((std::string(name) + "_output_socket_control").c_str())
        , m_txn(nullptr)
    {
        SCP_TRACE(()) << "constructor";

        SC_METHOD(sendall);
        sensitive << m_send_event;
        dont_initialize();
        output_control_socket.register_b_transport(this, &biflow_socket::initiator_ctrl);
        m_send_event.async_detach_suspending();
    }

    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_input_socket() { return input_socket; }
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_output_socket() { return output_socket; };
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_input_control_socket() { return input_control_socket; };
    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_output_control_socket() { return output_control_socket; };
    /**
     * @brief Bind method to connect two biflow sockets
     *
     * @tparam M
     * @param other : other socket
     */
    void bind(biflow_bindable& other)
    {
        SCP_TRACE(())("Binding {} to {}", name(), other.name());
        m_bound = true;
        other.new_bind(*this);
        output_socket.bind(other.get_input_socket());
        input_control_socket.bind(other.get_output_control_socket());
        other.get_output_socket().bind(input_socket);
        other.get_input_control_socket().bind(output_control_socket);
        other.bind_done();
    }

    /* target socket handlng */

    /**
     * @brief Register b_transport to be called whenever data is received from the socket.
     *
     * NOTE: this strips the 'quantum time' from the normal b_transport as it makes no sense in this
     * case.
     *
     * @param mod
     * @param cb
     */
    void register_b_transport(MODULE* mod, void (MODULE::*cb)(tlm::tlm_generic_payload&, sc_core::sc_time&))
    {
        input_socket.register_b_transport(mod, cb);
    }

    /**
     * @brief can_receive_more
     *
     * @param i number of _additional_ items that can now be received.
     */
    void can_receive_more(int i)
    {
        ctrl c;
        c.cmd = ctrl::DELTA_CHANGE;
        c.can_send = i;
        send_ctrl(c);
    }
    /**
     * @brief can_receive_set
     *
     * @param i number of items that can now be received.
     *
     * Setting this to zero will have the side effect of async_detach_suspending the other ends
     * async_event. Setting this to NON-zero will have the side effect of async_attach_suspending
     * the other ends async_event.
     */
    void can_receive_set(int i)
    {
        ctrl c;
        c.cmd = ctrl::ABSOLUTE_VALUE;
        c.can_send = i;
        send_ctrl(c);
    }
    /**
     * @brief can_receive_any
     * Allow unlimited items to arrive.
     */
    void can_receive_any()
    {
        ctrl c;
        c.cmd = ctrl::INFINITE_VALUE;
        send_ctrl(c);
    }

    /**
     * @brief enqueue
     * Enqueue data to be sent (unlimited queue size)
     * NOTE: Thread safe.
     * @param data
     */
    void enqueue(T data)
    {
        SCP_TRACE(())("Sending {}", data);
        std::lock_guard<std::mutex> guard(m_mutex);
        m_queue.push_back(data);
        m_send_event.notify();
    }

    /**
     * @brief set_default_txn
     * set transaction parameters (command, address and data_length)
     * @param txn
     */
    void set_default_txn(tlm::tlm_generic_payload& txn)
    {
        if (!m_txn) m_txn = std::make_unique<tlm::tlm_generic_payload>();
        m_txn->deep_copy_from(txn);
    }

    /**
     * @brief force_send
     * force send a transaction
     * @param txn
     */
    void force_send(tlm::tlm_generic_payload& txn)
    {
        sc_core::sc_time delay;
        output_socket->b_transport(txn, delay);
    }

    /**
     * @brief reset
     * Clear the current send queue.
     * NOTE: Thread safe.
     */
    void reset()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_queue.clear();
    }
};

/*
 * @class biflow_router_socket
 * Connect any number of biflow sockets together, routing data from any of them to all the others.
 * The router can always accept data (can_receive_any) since it will, itself,
 * pass the data onto another biflow socket, which will handle the control flow.
 */

template <class T = uint8_t>
class biflow_router_socket : public sc_core::sc_module, public biflow_multibindable
{
    SCP_LOGGER();

    /* Capture the socket ID by using a class */
    class remote : public sc_core::sc_object
    {
        sc_core::sc_vector<remote>& m_remotes;
        SCP_LOGGER();

    public:
        biflow_socket<remote, T> socket;
        remote* m_sendto = nullptr;

        void route_data(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
        {
            T* ptr = (T*)txn.get_data_ptr();
            bool sent = false;
            for (auto& remote : m_remotes) {
                if (&remote != this && (!m_sendto || m_sendto == &remote)) {
                    sent = true;
                    remote.socket.set_default_txn(txn);
                    for (int i = 0; i < txn.get_data_length(); i++) {
                        T data;
                        memcpy(&data, &(ptr[i]), sizeof(T));
                        remote.socket.enqueue(data);
                    }
                }
            }
            if (!sent) {
                SCP_WARN(())("Should have been sent - sendto {}", m_sendto->name());
            }
        }

        remote(sc_core::sc_module_name name, sc_core::sc_vector<struct remote>& r): socket(name), m_remotes(r)
        {
            socket.register_b_transport(this, &remote::route_data);
        }

        void set_sendto(remote* s) { m_sendto = s; }
    };

    sc_core::sc_vector<remote> m_remotes;

    biflow_socket<remote, T>* binding = nullptr;

    cci::cci_param<std::string> p_sendto;
    remote* m_sendto = nullptr;

    void sendto_setup(biflow_bindable& other, remote& remote)
    {
        if (!p_sendto.get_value().empty()) {
            if (m_sendto) {
                SCP_WARN(())("{} sendto {}", remote.name(), m_sendto->name());
                remote.set_sendto(m_sendto);
            } else if (std::string(other.name()).find(p_sendto) == 0) {
                m_sendto = &remote;
                SCP_WARN(())("found sendto {}", m_sendto->name());
                for (auto& r : m_remotes) {
                    if (&remote != &r) {
                        SCP_WARN(())("{} sendto {}", r.name(), m_sendto->name());
                        r.set_sendto(m_sendto);
                    }
                }
            } else {
                SCP_WARN(())("{} is not sendto {}", other.name(), p_sendto.get_value());
            }
        }
    }

public:
    void new_bind(biflow_bindable& other)
    {
        m_remotes.emplace_back(m_remotes);
        auto& remote = m_remotes[m_remotes.size() - 1];
        binding = &(remote.socket);
        binding->new_bind(other);
        sendto_setup(other, remote);
    }
    void bind_done(void)
    {
        sc_assert(binding != nullptr);
        binding->bind_done();
        binding->can_receive_any();
        binding = nullptr;
    }

    void bind(biflow_bindable& other)
    {
        SCP_TRACE(())("Biflow Router {} binding to {}", name(), other.name());
        m_remotes.emplace_back(m_remotes);

        auto& remote = m_remotes[m_remotes.size() - 1];
        binding = &(remote.socket);

        binding->bind(other); // This will call new_bind etc...
        binding->can_receive_any();
        binding = nullptr;

        sendto_setup(other, remote);
    }

    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_input_socket()
    {
        sc_assert(binding != nullptr);
        return binding->get_input_socket();
    }
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_output_socket()
    {
        sc_assert(binding != nullptr);
        return binding->get_output_socket();
    };
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_input_control_socket()
    {
        sc_assert(binding != nullptr);
        return binding->get_input_control_socket();
    };
    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_output_control_socket()
    {
        sc_assert(binding != nullptr);
        return binding->get_output_control_socket();
    };

    /**
     * @brief Construct a new biflow socket object
     *
     * @param name
     */
    struct ctrl {
        enum { DELTA_CHANGE, ABSOLUTE_VALUE, INFINITE_VALUE } cmd;
        uint32_t can_send;
    };
    biflow_router_socket(sc_core::sc_module_name name, std::string sendto = "")
        : sc_core::sc_module(name)
        , m_remotes("remote_sockets")
        , p_sendto("sendto", sendto, "Router all sent data to this port")
    {
        SCP_TRACE(()) << "constructor";
    }

    const char* name() { return sc_module::name(); }
};

template <class MODULE, class T = uint8_t>
class biflow_socket_multi : public sc_core::sc_module, public biflow_bindable
{
    SCP_LOGGER();
    biflow_socket<MODULE, T> main_socket;
    biflow_router_socket<T> router;

public:
    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_input_socket() { return router.get_input_socket(); }
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_output_socket() { return router.get_output_socket(); }
    tlm::tlm_initiator_socket<DEFAULT_TLM_BUSWIDTH>& get_input_control_socket()
    {
        return router.get_input_control_socket();
    }
    tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& get_output_control_socket()
    {
        return router.get_output_control_socket();
    }
    void new_bind(biflow_bindable& other) { router.new_bind(other); }
    void bind_done() { router.bind_done(); }
    void bind(biflow_bindable& other) { router.bind(other); }

    void register_b_transport(MODULE* mod, void (MODULE::*cb)(tlm::tlm_generic_payload&, sc_core::sc_time&))
    {
        main_socket.register_b_transport(mod, cb);
    }
    void can_receive_more(int i) { main_socket.can_receive_more(i); }
    void can_receive_set(int i) { main_socket.can_receive_set(i); }
    void can_receive_any() { main_socket.can_receive_any(); }

    void enqueue(T data) { main_socket.enqueue(data); }

    void set_default_txn(tlm::tlm_generic_payload& txn) { main_socket.set_default_txn(txn); }

    void force_send(tlm::tlm_generic_payload& txn) { main_socket.force_send(txn); }

    void reset() { main_socket.reset(); }

    biflow_socket_multi(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , main_socket((std::string(name) + "_main").c_str())
        , router((std::string(name) + "_router").c_str(), main_socket.name())
    {
        router.bind(main_socket);
    }

    const char* name() { return sc_module::name(); }
};

} // namespace gs

#endif
