/** @file
 *
 *  @ingroup test_module
 *
 *  @brief Test scenarios for @c make_endpoints functions.
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
#include <utility> // std::pair
#include <future>
#include <string_view>
#include <vector>

#include "net_ip/make_endpoints.hpp"
#include "net_ip/worker.hpp"

#include <iostream>

using namespace std::experimental::net;

template <typename Protocol>
void make_endpoints_test (bool local, std::string_view host, std::string_view port) {

  using res_results = ip::basic_resolver_results<Protocol>;
  using endpoints = std::vector<ip::basic_endpoint<Protocol> >;
  using prom_ret = std::pair<std::error_code, endpoints>;

  using namespace std::literals::chrono_literals;

  chops::net::worker wk;
  wk.start();

  ip::basic_resolver<Protocol> resolver(wk.get_io_context());

  GIVEN ("An executor work guard, host, and port strings") {
    WHEN ("async overload of make_endpoints is called") {
      THEN ("a sequence of endpoints is returned through a function object callback") {

        std::promise<prom_ret> res_prom;
        auto fut = res_prom.get_future();
        chops::net::make_endpoints<Protocol>(resolver,
          [p = std::move(res_prom)] (const std::error_code& err, res_results res) mutable {
std::cerr << "In lambda, ready to copy results, results size: " << res.size() << std::endl;
              endpoints ends { };
              for (const auto& i : res) {
                ends.push_back(i.endpoint());
              }
std::cerr << "In lambda, size of ends: " << ends.size() << std::endl;
              // res_prom.set_value(prom_ret(err, res));
              p.set_value(prom_ret(err, ends));
            }, local, host, port);
        auto a = fut.get();

        if (a.first) {
          INFO ("Error val: " << a.first);
        }
        REQUIRE_FALSE (a.second.empty());
        for (auto i : a.second) {
          INFO ("-- Endpoint: " << i);
        }
      }
    }
    AND_WHEN ("sync overload of make_endpoints is called") {
      THEN ("a sequence of endpoints is returned") {

        auto res = chops::net::make_endpoints<Protocol>(resolver, local, host, port);
        REQUIRE_FALSE (res.empty());
std::cerr << "Results size: " << res.size() << std::endl;
        for (const auto& i : res) {
std::cerr << "-- Endpoint: " << i.endpoint() << std::endl;
          INFO ("-- Endpoint: " << i.endpoint());
        }
      }
    }
  } // end given

  std::this_thread::sleep_for(5s); // sleep for 5 seconds
  wk.reset();

}

SCENARIO ( "Make endpoints remote test, TCP  1", "[tcp_make_endpoints_1]" ) {

  make_endpoints_test<ip::tcp> (false, "www.cnn.com", "80");

}

SCENARIO ( "Make endpoints remote test, TCP 2", "[tcp_make_endpoints_2]" ) {

  make_endpoints_test<ip::tcp> (false, "www.seattletimes.com", "80");

}

SCENARIO ( "Make endpoints local test, TCP 3", "[tcp_make_endpoints_3]" ) {

  make_endpoints_test<ip::tcp> (true, "", "23000");

}

SCENARIO ( "Make endpoints remote test, UDP  1", "[udp_make_endpoints_1]" ) {

  make_endpoints_test<ip::udp> (false, "www.cnn.com", "80");

}

