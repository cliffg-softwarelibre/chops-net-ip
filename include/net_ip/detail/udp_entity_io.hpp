/** @file 
 *
 *  @ingroup net_ip_module
 *
 *  @brief Internal class that combines a UDP entity and UDP io handler.
 *
 *  @note For internal use only.
 *
 *  @author Cliff Green
 *
 *  Copyright (c) 2018 by Cliff Green
 *
 *  Distributed under the Boost Software License, Version 1.0. 
 *  (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 *
 */

#ifndef UDP_ENTITY_IO_HPP_INCLUDED
#define UDP_ENTITY_IO_HPP_INCLUDED

#include <experimental/executor>
#include <experimental/io_context>
#include <experimental/internet>
#include <experimental/buffer>

#include <memory> // std::shared_ptr, std::enable_shared_from_this
#include <system_error>

#include <cstddef> // std::size_t
#include <utility> // std::forward, std::move

#include "net_ip/detail/io_common.hpp"
#include "net_ip/detail/net_entity_common.hpp"
#include "net_ip/detail/output_queue.hpp"

#include "net_ip/queue_stats.hpp"
#include "net_ip/net_ip_error.hpp"
#include "net_ip/basic_io_interface.hpp"
#include "utility/shared_buffer.hpp"

namespace chops {
namespace net {
namespace detail {

class udp_entity_io : public std::enable_shared_from_this<udp_entity_io> {
public:
  using socket_type = std::experimental::net::ip::udp::socket;
  using endpoint_type = std::experimental::net::ip::udp::endpoint;

private:
  using byte_vec = chops::mutable_shared_buffer::byte_vec;

private:

  io_common<udp_entity_io>          m_io_common;
  net_entity_common<udp_entity_io>  m_entity_common;
  socket_type                       m_socket;
  endpoint_type                     m_local_endp;
  endpoint_type                     m_default_dest_endp;
  // TODO: multicast stuff

  // following members could be passed through handler, but are members for 
  // simplicity and less copying
  byte_vec                          m_byte_vec;
  std::size_t                       m_max_size;
  endpoint_type                     m_sender_endp;

public:
  udp_entity_io(std::experimental::net::io_context& ioc, 
                const endpoint_type& local_endp) noexcept : 
    m_io_common(), m_entity_common(), 
    m_socket(ioc), m_local_endp(local_endp), m_default_dest_endp(), 
    m_byte_vec(), m_max_size(0), m_sender_endp() { }

private:
  // no copy or assignment semantics for this class
  udp_entity_io(const udp_entity_io&) = delete;
  udp_entity_io(udp_entity_io&&) = delete;
  udp_entity_io& operator=(const udp_entity_io&) = delete;
  udp_entity_io& operator=(udp_entity_io&&) = delete;

public:

  // all of the methods in this public section can be called through either an io_interface
  // or a net_entity

  bool is_started() const noexcept { return m_entity_common.is_started(); }

  bool is_io_started() const noexcept { return m_io_common.is_io_started(); }

  socket_type& get_socket() noexcept { return m_socket; }

  output_queue_stats get_output_queue_stats() const noexcept {
    return m_io_common.get_output_queue_stats();
  }

  template <typename F1, typename F2>
  bool start(F1&& io_state_chg, F2&& err_cb) {
    if (!m_entity_common.start(std::forward<F1>(io_state_chg), std::forward<F2>(err_cb))) {
      // already started
      return false;
    }
    try {
      // assume default constructed endpoints compare equal
      if (m_local_endp == endpoint_type()) {
// TODO: this needs to be changed, doesn't allow sending to an ipV6 endpoint
        m_socket.open(std::experimental::net::ip::udp::v4());
      }
      else {
        m_socket = socket_type(m_socket.get_executor().context(), m_local_endp);
      }
    }
    catch (const std::system_error& se) {
      err_notify(se.code());
      stop();
      return false;
    }
    m_entity_common.call_io_state_chg_cb(shared_from_this(), 1, true);
    return true;
  }

  template <typename MH>
  bool start_io(std::size_t max_size, MH&& msg_handler) {
    if (!m_io_common.set_io_started()) { // concurrency protected
      return false;
    }
    m_max_size = max_size;
    start_read(std::forward<MH>(msg_handler));
    return true;
  }

  template <typename MH>
  bool start_io(const endpoint_type& endp, std::size_t max_size, MH&& msg_handler) {
    if (!m_io_common.set_io_started()) { // concurrency protected
      return false;
    }
    m_max_size = max_size;
    m_default_dest_endp = endp;
    start_read(std::forward<MH>(msg_handler));
    return true;
  }

  bool start_io() {
    if (!m_io_common.set_io_started()) { // concurrency protected
      return false;
    }
    return true;
  }

  bool start_io(const endpoint_type& endp) {
    if (!m_io_common.set_io_started()) { // concurrency protected
      return false;
    }
    m_default_dest_endp = endp;
    return true;
  }

  bool stop_io() {
    if (!m_io_common.stop()) {
      return false;
    }
    std::error_code ec;
    m_socket.close(ec);
    err_notify(std::make_error_code(net_ip_errc::udp_io_handler_stopped));
    m_entity_common.call_io_state_chg_cb(shared_from_this(), 0, false);
    return true;
  }

  bool stop() {
    if (!m_entity_common.stop()) {
      return false; // stop already called
    }
    stop_io();
    err_notify(std::make_error_code(net_ip_errc::udp_entity_stopped));
    return true;
  }

  void send(chops::const_shared_buffer buf) {
    auto self { shared_from_this() };
    post(m_socket.get_executor(), [this, self, buf] {
        if (!m_io_common.start_write_setup(buf)) {
          return; // buf queued or shutdown happening
        }
        start_write(buf, m_default_dest_endp);
      }
    );
  }

  void send(chops::const_shared_buffer buf, const endpoint_type& endp) {
    auto self { shared_from_this() };
    post(m_socket.get_executor(), [this, self, buf, endp] {
        if (!m_io_common.start_write_setup(buf, endp)) {
          return; // buf queued or shutdown happening
        }
        start_write(buf, endp);
      }
    );
  }

private:

  template <typename MH>
  void start_read(MH&& msg_hdlr) {
    auto self { shared_from_this() };
    m_byte_vec.resize(m_max_size);
    m_socket.async_receive_from(
              std::experimental::net::mutable_buffer(m_byte_vec.data(), m_byte_vec.size()),
              m_sender_endp,
                [this, self, mh = std::move(msg_hdlr)] 
                  (const std::error_code& err, std::size_t nb) mutable {
        handle_read(err, nb, mh);
      }
    );
  }

  void err_notify (const std::error_code& err) {
    m_entity_common.call_error_cb(shared_from_this(), err);
  }

  template <typename MH>
  void handle_read(const std::error_code&, std::size_t, MH&&);

  void start_write(chops::const_shared_buffer, const endpoint_type&);

  void handle_write(const std::error_code&, std::size_t);

};

// method implementations, just to make the class declaration a little more readable

template <typename MH>
void udp_entity_io::handle_read(const std::error_code& err, std::size_t num_bytes, MH&& msg_hdlr) {

  if (err) {
    err_notify(err);
    stop();
    return;
  }
  if (!msg_hdlr(std::experimental::net::const_buffer(m_byte_vec.data(), num_bytes), 
                basic_io_interface<udp_entity_io>(weak_from_this()), m_sender_endp)) {
    // message handler not happy, tear everything down
    err_notify(std::make_error_code(net_ip_errc::message_handler_terminated));
    stop();
    return;
  }
  start_read(std::forward<MH>(msg_hdlr));
}

inline void udp_entity_io::start_write(chops::const_shared_buffer buf, const endpoint_type& endp) {
  auto self { shared_from_this() };
  m_socket.async_send_to(std::experimental::net::const_buffer(buf.data(), buf.size()), endp,
            [this, self] (const std::error_code& err, std::size_t nb) {
      handle_write(err, nb);
    }
  );
}

inline void udp_entity_io::handle_write(const std::error_code& err, std::size_t /* num_bytes */) {
  if (err) {
    err_notify(err);
    stop();
    return;
  }
  auto elem = m_io_common.get_next_element();
  if (!elem) {
    return;
  }
  start_write(elem->first, elem->second ? *(elem->second) : m_default_dest_endp);
}

using udp_entity_io_ptr = std::shared_ptr<udp_entity_io>;

} // end detail namespace
} // end net namespace
} // end chops namespace

#endif

