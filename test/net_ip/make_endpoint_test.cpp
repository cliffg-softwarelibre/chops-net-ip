/** @file
 *
 *  @ingroup test_module
 *
 *  @brief Test scenarios for @c make_endpoint and @c resolve_endpoint functions.
 *
 *  @author Cliff Green
 *  @date 2018
 *  @copyright Cliff Green, MIT License
 *
 */

#include "catch.hpp"

#include <experimental/internet>
#include <experimental/socket>
#include <experimental/io_context>

#include <system_error> // std::error_code
#include <cstddef> // std::size_t
#include <memory> // std::make_shared
#include <utility> // std::move
#include <tuple>
#include <thread>
#include <future>
#include <chrono>
#include <functional>
#include <string_view>

#include "net_ip/make_endpoint.hpp"
#include "net_ip/worker.hpp"


using namespace std::experimental::net;

constexpr int test_port = 30434;
const char*   test_addr = "127.0.0.1";
constexpr int NumMsgs = 50;


// Catch test framework is not thread-safe, therefore all REQUIRE clauses must be in a single 
// thread; return data: 1. error code 2. Does the ioh ptr match? 3. msg set size
using thread_data = std::tuple<std::error_code, bool, std::size_t>; 
using thread_promise = std::promise<thread_data>;

using tcp_ioh_ptr = std::shared_ptr<chops::net::detail::tcp_io>;

void connector_func (thread_promise thr_prom, const vec_buf& in_msg_set, io_context& ioc, 
                     int interval, std::string_view delim, chops::const_shared_buffer empty_msg) {
  using namespace std::placeholders;

  entity_notifier en { };
  auto en_fut = en.prom.get_future();
  notifier_cb cb(std::bind(&entity_notifier::notify_me, &en, _1, _2));

  ip::tcp::socket sock(ioc);
  sock.connect(ip::tcp::endpoint(ip::make_address(test_addr), test_port));

  auto iohp = std::make_shared<chops::net::detail::tcp_io>(std::move(sock), cb);
  vec_buf vb;
  if (delim.empty()) {
    iohp->start_io(msg_hdlr<chops::net::detail::tcp_io>(vb, false), 
                   chops::net::make_simple_variable_len_msg_frame(decode_variable_len_msg_hdr), 2);
  }
  else {
    iohp->start_io(msg_hdlr<chops::net::detail::tcp_io>(vb, false), delim);
  }

  for (auto buf : in_msg_set) {
    iohp->send(chops::const_shared_buffer(std::move(buf)));
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
  }
  iohp->send(empty_msg);

  auto ret = en_fut.get(); // wait for termination callback
  thr_prom.set_value(thread_data(std::get<0>(ret), std::get<1>(ret) == iohp, vb.size()));

}

void acc_conn_test (const vec_buf& in_msg_set, bool reply, int interval, std::string_view delim,
                    chops::const_shared_buffer empty_msg) {

  using namespace std::placeholders;

  chops::net::worker wk;
  wk.start();

  GIVEN ("An executor work guard and a message set") {
 
    WHEN ("an acceptor and connector are created") {
      THEN ("the futures provide synchronization and data returns") {

        ip::tcp::acceptor acc(wk.get_io_context(), ip::tcp::endpoint(ip::address_v4::any(), test_port));

        entity_notifier en { };
        auto en_fut = en.prom.get_future();
        notifier_cb cb(std::bind(&entity_notifier::notify_me, &en, _1, _2));

        INFO ("Creating connector thread, msg interval: " << interval);

        thread_promise conn_prom;
        auto conn_fut = conn_prom.get_future();
        std::thread conn_thr(connector_func, std::move(conn_prom), std::cref(in_msg_set), 
                             std::ref(wk.get_io_context()), interval, delim, empty_msg);

        auto iohp = std::make_shared<chops::net::detail::tcp_io>(std::move(acc.accept()), cb);
        vec_buf vb;
        if (delim.empty()) {
          iohp->start_io(msg_hdlr<chops::net::detail::tcp_io>(vb, reply), 
                         chops::net::make_simple_variable_len_msg_frame(decode_variable_len_msg_hdr), 2);
        }
        else {
          iohp->start_io(msg_hdlr<chops::net::detail::tcp_io>(vb, reply), delim);
        }

        auto en_ret = en_fut.get(); // wait for termination callback
        INFO ("Entity future popped");

        auto conn_data = conn_fut.get();
        INFO ("Connector thread future popped, joining thread");
        conn_thr.join();
        std::error_code e = std::get<0>(en_ret);
        INFO ("Acceptor error code and msg: " << e << " " << e.message() );
        e = std::get<0>(conn_data);
        INFO ("Connector error code and msg: " << e << " " << e.message());
        REQUIRE (std::get<1>(en_ret) == iohp);
        REQUIRE (std::get<1>(conn_data));
        REQUIRE (in_msg_set == vb);
        if (reply) {
          REQUIRE (std::get<2>(conn_data) == in_msg_set.size());
        }
        else {
          REQUIRE (std::get<2>(conn_data) == 0);
        }
      }
    }
  } // end given
  wk.stop();

}


SCENARIO ( "Tcp IO handler test, variable len msgs, interval 50, one-way", "[tcp_io_one_way_var]" ) {

  auto ms = make_msg_set (make_variable_len_msg, "Heehaw!", 'Q', NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_variable_len_msg));
  acc_conn_test ( ms, false, 50, std::string_view(), empty_msg );

}

SCENARIO ( "Tcp IO handler test, variable len msgs, interval 0, one-way", "[tcp_io_one_way_var_fast]" ) {

  auto ms = make_msg_set (make_variable_len_msg, "Haw!", 'R', 2*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_variable_len_msg));
  acc_conn_test ( ms, false, 0, std::string_view(), empty_msg );

}

SCENARIO ( "Tcp IO handler test, variable len msgs, interval 50, two-way", "[tcp_io_two_way_var]" ) {

  auto ms = make_msg_set (make_variable_len_msg, "Yowser!", 'X', NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_variable_len_msg));
  acc_conn_test ( ms, true, 50, std::string_view(), empty_msg );

}

SCENARIO ( "Tcp IO handler test, variable len msgs, interval 0, two-way, many msgs", "[tcp_io_two_way_var_fast_many]" ) {

  auto ms = make_msg_set (make_variable_len_msg, "Whoah, fast!", 'X', 100*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_variable_len_msg));
  acc_conn_test ( ms, true, 0, std::string_view(), empty_msg );

}

SCENARIO ( "Tcp IO handler test, CR / LF msgs, interval 50, one-way", "[tcp_io_one_way_cr_lf]" ) {

  auto ms = make_msg_set (make_cr_lf_text_msg, "Hohoho!", 'Q', NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_cr_lf_text_msg));
  acc_conn_test ( ms, false, 50, std::string_view("\r\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, CR / LF msgs, interval 0, one-way", "[tcp_io_one_way_cr_lf_fast]" ) {

  auto ms = make_msg_set (make_cr_lf_text_msg, "HawHeeHaw!", 'N', 4*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_cr_lf_text_msg));
  acc_conn_test ( ms, false, 0, std::string_view("\r\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, CR / LF msgs, interval 30, two-way", "[tcp_io_two_way_cr_lf]" ) {

  auto ms = make_msg_set (make_cr_lf_text_msg, "Yowzah!", 'G', 5*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_cr_lf_text_msg));
  acc_conn_test ( ms, true, 30, std::string_view("\r\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, CR / LF msgs, interval 0, two-way, many msgs", "[tcp_io_two_way_cr_lf_fast_many]" ) {

  auto ms = make_msg_set (make_cr_lf_text_msg, "Yes, yes, very fast!", 'F', 200*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_cr_lf_text_msg));
  acc_conn_test ( ms, true, 0, std::string_view("\r\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, LF msgs, interval 50, one-way", "[tcp_io_one_way_lf]" ) {

  auto ms = make_msg_set (make_lf_text_msg, "Excited!", 'E', NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_lf_text_msg));
  acc_conn_test ( ms, false, 50, std::string_view("\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, LF msgs, interval 0, one-way", "[tcp_io_one_way_lf_fast]" ) {

  auto ms = make_msg_set (make_lf_text_msg, "Excited fast!", 'F', 6*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_lf_text_msg));
  acc_conn_test ( ms, false, 0, std::string_view("\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, LF msgs, interval 20, two-way", "[tcp_io_two_way_lf]" ) {

  auto ms = make_msg_set (make_lf_text_msg, "Whup whup!", 'T', 2*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_lf_text_msg));
  acc_conn_test ( ms, true, 20, std::string_view("\n"), empty_msg );

}

SCENARIO ( "Tcp IO handler test, LF msgs, interval 0, two-way, many msgs", "[tcp_io_two_way_lf_fast_many]" ) {

  auto ms = make_msg_set (make_lf_text_msg, "Super fast!", 'S', 300*NumMsgs);
  chops::const_shared_buffer empty_msg(make_empty_body_msg(make_lf_text_msg));
  acc_conn_test ( ms, true, 0, std::string_view("\n"), empty_msg );

}