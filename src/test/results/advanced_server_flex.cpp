//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: Advanced server, flex (plain + SSL)
//
//------------------------------------------------------------------------------

#include "example/common/detect_ssl.hpp"
#include "example/common/server_certificate.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/_experimental/core/ssl_stream.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/make_unique.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;                 // from <boost/beast.hpp>
namespace http = beast::http;                   // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
namespace net = boost::asio;                    // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

// Return a reasonable mime type based on the extension of a file.
beast::string_view
mime_type(beast::string_view path)
{ @_0_
    using beast::iequals;
    auto const ext = [&path]
    { @_1_
        auto const pos = path.rfind(".");
        if(pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }(); @_1_
    if(iequals(ext, ".htm"))  return "text/html";
    if(iequals(ext, ".html")) return "text/html";
    if(iequals(ext, ".php"))  return "text/html";
    if(iequals(ext, ".css"))  return "text/css";
    if(iequals(ext, ".txt"))  return "text/plain";
    if(iequals(ext, ".js"))   return "application/javascript";
    if(iequals(ext, ".json")) return "application/json";
    if(iequals(ext, ".xml"))  return "application/xml";
    if(iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if(iequals(ext, ".flv"))  return "video/x-flv";
    if(iequals(ext, ".png"))  return "image/png";
    if(iequals(ext, ".jpe"))  return "image/jpeg";
    if(iequals(ext, ".jpeg")) return "image/jpeg";
    if(iequals(ext, ".jpg"))  return "image/jpeg";
    if(iequals(ext, ".gif"))  return "image/gif";
    if(iequals(ext, ".bmp"))  return "image/bmp";
    if(iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if(iequals(ext, ".tiff")) return "image/tiff";
    if(iequals(ext, ".tif"))  return "image/tiff";
    if(iequals(ext, ".svg"))  return "image/svg+xml";
    if(iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
} @_0_

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
    beast::string_view base,
    beast::string_view path)
{ @_2_
    if(base.empty())
        return std::string(path);
    std::string result(base);
#if BOOST_MSVC @_3_
    char constexpr path_separator = '\\';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for(auto& c : result)
        if(c == '/')
            c = path_separator; @_3_
#else @_4_
    char constexpr path_separator = '/';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif @_4_
    return result;
} @_2_

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
    class Body, class Allocator,
    class Send>
void
handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{ @_5_
    // Returns a bad request response
    auto const bad_request =
    [&req](beast::string_view why)
    { @_6_
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    }; @_6_

    // Returns a not found response
    auto const not_found =
    [&req](beast::string_view target)
    { @_7_
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    }; @_7_

    // Returns a server error response
    auto const server_error =
    [&req](beast::string_view what)
    { @_8_
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    }; @_8_

    // Make sure we can handle the method
    if( req.method() != http::verb::get &&
        req.method() != http::verb::head)
        return send(bad_request("Unknown HTTP-method"));

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return send(bad_request("Illegal request-target"));

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    if(req.target().back() == '/')
        path.append("index.html");

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if(ec == beast::errc::no_such_file_or_directory)
        return send(not_found(req.target()));

    // Handle an unknown error
    if(ec)
        return send(server_error(ec.message()));

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if(req.method() == http::verb::head)
    { @_9_
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    } @_9_

    // Respond to GET request
    http::response<http::file_body> res{ @_10_
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())}; @_10_
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
} @_5_

//------------------------------------------------------------------------------

// Report a failure
void
fail(beast::error_code ec, char const* what)
{ @_11_
    std::cerr << what << ": " << ec.message() << "\n";
} @_11_

//------------------------------------------------------------------------------

// Echoes back all received WebSocket messages.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class websocket_session
{ @_12_
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived&
    derived()
    { @_13_
        return static_cast<Derived&>(*this);
    } @_13_

    beast::flat_buffer buffer_;
    char ping_state_ = 0;

protected:
    net::steady_timer timer_;

public:
    // Construct the session
    explicit
    websocket_session(net::io_context& ioc)
        : timer_(ioc,
            (std::chrono::steady_clock::time_point::max)())
    { @_14_
    } @_14_

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void
    do_accept(http::request<Body, http::basic_fields<Allocator>> req)
    { @_15_
        // Set the control callback. This will be called
        // on every incoming ping, pong, and close frame.
        derived().ws().control_callback(
            std::bind(
                &websocket_session::on_control_callback,
                this,
                std::placeholders::_1,
                std::placeholders::_2));

        // VFALCO What about the timer?

        // Accept the websocket handshake
        derived().ws().async_accept(
            req,
            std::bind(
                &websocket_session::on_accept,
                derived().shared_from_this(),
                std::placeholders::_1));
    } @_15_

    void
    on_accept(beast::error_code ec)
    { @_16_
        // Happens when the timer closes the socket
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "accept");

        // Read a message
        do_read();
    } @_16_

    // Called when the timer expires.
    void
    on_timer(beast::error_code ec)
    { @_17_
        if(ec && ec != net::error::operation_aborted)
            return fail(ec, "timer");

        // See if the timer really expired since the deadline may have moved.
        if(timer_.expiry() <= std::chrono::steady_clock::now())
        { @_18_
            // If this is the first time the timer expired,
            // send a ping to see if the other end is there.
            if(derived().ws().is_open() && ping_state_ == 0)
            { @_19_
                // Note that we are sending a ping
                ping_state_ = 1;

                // Set the timer
                timer_.expires_after(std::chrono::seconds(15));

                // Now send the ping
                derived().ws().async_ping({},
                    std::bind(
                        &websocket_session::on_ping,
                        derived().shared_from_this(),
                        std::placeholders::_1));
            } @_19_
            else
            { @_20_
                // The timer expired while trying to handshake,
                // or we sent a ping and it never completed or
                // we never got back a control frame, so close.

                derived().do_timeout();
                return;
            } @_20_
        } @_18_

        // Wait on the timer
        timer_.async_wait(
            net::bind_executor(
                derived().ws().get_executor(), // use the strand
                std::bind(
                    &websocket_session::on_timer,
                    derived().shared_from_this(),
                    std::placeholders::_1)));
    } @_17_

    // Called to indicate activity from the remote peer
    void
    activity()
    { @_21_
        // Note that the connection is alive
        ping_state_ = 0;

        // Set the timer
        timer_.expires_after(std::chrono::seconds(15));
    } @_21_

    // Called after a ping is sent.
    void
    on_ping(beast::error_code ec)
    { @_22_
        // Happens when the timer closes the socket
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "ping");

        // Note that the ping was sent.
        if(ping_state_ == 1)
        { @_23_
            ping_state_ = 2;
        } @_23_
        else
        { @_24_
            // ping_state_ could have been set to 0
            // if an incoming control frame was received
            // at exactly the same time we sent a ping.
            BOOST_ASSERT(ping_state_ == 0);
        } @_24_
    } @_22_

    void
    on_control_callback(
        websocket::frame_type kind,
        beast::string_view payload)
    { @_25_
        boost::ignore_unused(kind, payload);

        // Note that there is activity
        activity();
    } @_25_

    void
    do_read()
    { @_26_
        // Read a message into our buffer
        derived().ws().async_read(
            buffer_,
            std::bind(
                &websocket_session::on_read,
                derived().shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2));
    } @_26_

    void
    on_read(
        beast::error_code ec,
        std::size_t bytes_transferred)
    { @_27_
        boost::ignore_unused(bytes_transferred);

        // Happens when the timer closes the socket
        if(ec == net::error::operation_aborted)
            return;

        // This indicates that the websocket_session was closed
        if(ec == websocket::error::closed)
            return;

        if(ec)
            fail(ec, "read");

        // Note that there is activity
        activity();

        // Echo the message
        derived().ws().text(derived().ws().got_text());
        derived().ws().async_write(
            buffer_.data(),
            std::bind(
                &websocket_session::on_write,
                derived().shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2));
    } @_27_

    void
    on_write(
        beast::error_code ec,
        std::size_t bytes_transferred)
    { @_28_
        boost::ignore_unused(bytes_transferred);

        // Happens when the timer closes the socket
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "write");

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Do another read
        do_read();
    } @_28_
}; @_12_

// Handles a plain WebSocket connection
class plain_websocket_session
    : public websocket_session<plain_websocket_session>
    , public std::enable_shared_from_this<plain_websocket_session>
{ @_29_
    websocket::stream<
        beast::tcp_stream<net::io_context::strand>> ws_;
    bool close_ = false;

public:
    // Create the session
    explicit
    plain_websocket_session(
        beast::tcp_stream<net::io_context::strand>&& stream)
        : websocket_session<plain_websocket_session>(
            stream.get_executor().context())
        , ws_(std::move(stream))
    { @_30_
    } @_30_

    // Called by the base class
    websocket::stream<
        beast::tcp_stream<net::io_context::strand>>&
    ws()
    { @_31_
        return ws_;
    } @_31_

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void
    run(http::request<Body, http::basic_fields<Allocator>> req)
    { @_32_
        // Run the timer. The timer is operated
        // continuously, this simplifies the code.
        on_timer({});

        // Accept the WebSocket upgrade request
        do_accept(std::move(req));
    } @_32_

    void
    do_timeout()
    { @_33_
        // This is so the close can have a timeout
        if(close_)
            return;
        close_ = true;

        // VFALCO This doesn't look right...
        // Set the timer
        timer_.expires_after(std::chrono::seconds(15));

        // Close the WebSocket Connection
        ws_.async_close(
            websocket::close_code::normal,
            std::bind(
                &plain_websocket_session::on_close,
                shared_from_this(),
                std::placeholders::_1));
    } @_33_

    void
    on_close(beast::error_code ec)
    { @_34_
        // Happens when close times out
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "close");

        // At this point the connection is gracefully closed
    } @_34_
}; @_29_

// Handles an SSL WebSocket connection
class ssl_websocket_session
    : public websocket_session<ssl_websocket_session>
    , public std::enable_shared_from_this<ssl_websocket_session>
{ @_35_
    websocket::stream<beast::ssl_stream<
        beast::tcp_stream<net::io_context::strand>>> ws_;
    bool eof_ = false;

public:
    // Create the http_session
    explicit
    ssl_websocket_session(beast::ssl_stream<
            beast::tcp_stream<net::io_context::strand>>&& stream)
        : websocket_session<ssl_websocket_session>(
            stream.get_executor().context())
        , ws_(std::move(stream))
    { @_36_
    } @_36_

    // Called by the base class
    websocket::stream<beast::ssl_stream<
        beast::tcp_stream<net::io_context::strand>>>&
    ws()
    { @_37_
        return ws_;
    } @_37_

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void
    run(http::request<Body, http::basic_fields<Allocator>> req)
    { @_38_
        // Run the timer. The timer is operated
        // continuously, this simplifies the code.
        on_timer({});

        // Accept the WebSocket upgrade request
        do_accept(std::move(req));
    } @_38_

    void
    do_eof()
    { @_39_
        eof_ = true;

        // Set the timer
        timer_.expires_after(std::chrono::seconds(15));

        // Perform the SSL shutdown
        ws_.next_layer().async_shutdown(
            std::bind(
                &ssl_websocket_session::on_shutdown,
                shared_from_this(),
                std::placeholders::_1));
    } @_39_

    void
    on_shutdown(beast::error_code ec)
    { @_40_
        // Happens when the shutdown times out
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "shutdown");

        // At this point the connection is closed gracefully
    } @_40_

    void
    do_timeout()
    { @_41_
        // If this is true it means we timed out performing the shutdown
        if(eof_)
            return;

        // Start the timer again
        timer_.expires_at(
            (std::chrono::steady_clock::time_point::max)());
        on_timer({});
        do_eof();
    } @_41_
}; @_35_

template<class Body, class Allocator>
void
make_websocket_session(
    beast::tcp_stream<net::io_context::strand> stream,
    http::request<Body, http::basic_fields<Allocator>> req)
{ @_42_
    std::make_shared<plain_websocket_session>(
        std::move(stream))->run(std::move(req));
} @_42_

template<class Body, class Allocator>
void
make_websocket_session(
    beast::ssl_stream<beast::tcp_stream<net::io_context::strand>> stream,
    http::request<Body, http::basic_fields<Allocator>> req)
{ @_43_
    std::make_shared<ssl_websocket_session>(
        std::move(stream))->run(std::move(req));
} @_43_

//------------------------------------------------------------------------------

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class http_session
{ @_44_
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived&
    derived()
    { @_45_
        return static_cast<Derived&>(*this);
    } @_45_

    // This queue is used for HTTP pipelining.
    class queue
    { @_46_
        enum
        { @_47_
            // Maximum number of responses we will queue
            limit = 8
        }; @_47_

        // The type-erased, saved work item
        struct work
        { @_48_
            virtual ~work() = default;
            virtual void operator()() = 0;
        }; @_48_

        http_session& self_;
        std::vector<std::unique_ptr<work>> items_;

    public:
        explicit
        queue(http_session& self)
            : self_(self)
        { @_49_
            static_assert(limit > 0, "queue limit must be positive");
            items_.reserve(limit);
        } @_49_

        // Returns `true` if we have reached the queue limit
        bool
        is_full() const
        { @_50_
            return items_.size() >= limit;
        } @_50_

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool
        on_write()
        { @_51_
            BOOST_ASSERT(! items_.empty());
            auto const was_full = is_full();
            items_.erase(items_.begin());
            if(! items_.empty())
                (*items_.front())();
            return was_full;
        } @_51_

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void
        operator()(http::message<isRequest, Body, Fields>&& msg)
        { @_52_
            // This holds a work item
            struct work_impl : work
            { @_53_
                http_session& self_;
                http::message<isRequest, Body, Fields> msg_;

                work_impl(
                    http_session& self,
                    http::message<isRequest, Body, Fields>&& msg)
                    : self_(self)
                    , msg_(std::move(msg))
                { @_54_
                } @_54_

                void
                operator()()
                { @_55_
                    http::async_write(
                        self_.derived().stream(),
                        msg_,
                        std::bind(
                            &http_session::on_write,
                            self_.derived().shared_from_this(),
                            std::placeholders::_1,
                            msg_.need_eof()));
                } @_55_
            }; @_53_

            // Allocate and store the work
            items_.push_back(
                boost::make_unique<work_impl>(self_, std::move(msg)));

            // If there was no previous work, start this one
            if(items_.size() == 1)
                (*items_.front())();
        } @_52_
    }; @_46_

    std::shared_ptr<std::string const> doc_root_;
    http::request<http::string_body> req_;
    queue queue_;

protected:
    net::steady_timer timer_;
    beast::flat_buffer buffer_;

public:
    // Construct the session
    http_session(
        net::io_context& ioc,
        beast::flat_buffer buffer,
        std::shared_ptr<std::string const> const& doc_root)
        : doc_root_(doc_root)
        , queue_(*this)
        , timer_(ioc,
            (std::chrono::steady_clock::time_point::max)())
        , buffer_(std::move(buffer))
    { @_56_
    } @_56_

    void
    do_read()
    { @_57_
        // Make the request empty before reading,
        // otherwise the operation behavior is undefined.
        req_ = {};

        // Set the timeout.
        beast::get_lowest_layer(
            derived().stream()).expires_after(std::chrono::seconds(30));

        // Read a request
        http::async_read(
            derived().stream(),
            buffer_,
            req_,
            std::bind(
                &http_session::on_read,
                derived().shared_from_this(),
                std::placeholders::_1));
    } @_57_

    void
    on_read(beast::error_code ec)
    { @_58_
        // This means they closed the connection
        if(ec == http::error::end_of_stream)
            return derived().do_eof();

        if(ec)
            return fail(ec, "read");

        // See if it is a WebSocket Upgrade
        if(websocket::is_upgrade(req_))
        { @_59_
            // Transfer the stream to a new WebSocket session
            return make_websocket_session(
                derived().release_stream(),
                std::move(req_));
        } @_59_

        // Send the response
        handle_request(*doc_root_, std::move(req_), queue_);

        // If we aren't at the queue limit, try to pipeline another request
        if(! queue_.is_full())
            do_read();
    } @_58_

    void
    on_write(beast::error_code ec, bool close)
    { @_60_
        // Happens when the timer closes the socket
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "write");

        if(close)
        { @_61_
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return derived().do_eof();
        } @_61_

        // Inform the queue that a write completed
        if(queue_.on_write())
        { @_62_
            // Read another request
            do_read();
        } @_62_
    } @_60_
}; @_44_

// Handles a plain HTTP connection
class plain_http_session
    : public http_session<plain_http_session>
    , public std::enable_shared_from_this<plain_http_session>
{ @_63_
    beast::tcp_stream<net::io_context::strand> stream_;

public:
    // Create the http_session
    plain_http_session(
        beast::tcp_stream<net::io_context::strand>&& stream,
        beast::flat_buffer&& buffer,
        std::shared_ptr<std::string const> const& doc_root)
        : http_session<plain_http_session>(
            stream.get_executor().context(),
            std::move(buffer),
            doc_root)
        , stream_(std::move(stream))
    { @_64_
    } @_64_

    // Called by the base class
    beast::tcp_stream<net::io_context::strand>&
    stream()
    { @_65_
        return stream_;
    } @_65_

    // Called by the base class
    beast::tcp_stream<net::io_context::strand>
    release_stream()
    { @_66_
        return std::move(stream_);
    } @_66_

    // Start the asynchronous operation
    void
    run()
    { @_67_
        // Make sure we run on the strand
        if(! stream_.get_executor().running_in_this_thread())
            return net::post(
                stream_.get_executor(),
                std::bind(
                    &plain_http_session::run,
                    shared_from_this()));

        do_read();
    } @_67_

    void
    do_eof()
    { @_68_
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    } @_68_

    void
    do_timeout()
    { @_69_
        // Closing the socket cancels all outstanding operations. They
        // will complete with net::error::operation_aborted
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        stream_.socket().close(ec);
    } @_69_
}; @_63_

// Handles an SSL HTTP connection
class ssl_http_session
    : public http_session<ssl_http_session>
    , public std::enable_shared_from_this<ssl_http_session>
{ @_70_
    beast::ssl_stream<
        beast::tcp_stream<net::io_context::strand>> stream_;
    bool eof_ = false;

public:
    // Create the http_session
    ssl_http_session(
        beast::tcp_stream<net::io_context::strand>&& stream,
        ssl::context& ctx,
        beast::flat_buffer&& buffer,
        std::shared_ptr<std::string const> const& doc_root)
        : http_session<ssl_http_session>(
            stream.get_executor().context(),
            std::move(buffer),
            doc_root)
        , stream_(std::move(stream), ctx)
    { @_71_
    } @_71_

    // Called by the base class
    beast::ssl_stream<
        beast::tcp_stream<net::io_context::strand>>&
    stream()
    { @_72_
        return stream_;
    } @_72_

    // Called by the base class
    beast::ssl_stream<
        beast::tcp_stream<net::io_context::strand>>
    release_stream()
    { @_73_
        return std::move(stream_);
    } @_73_

    // Start the asynchronous operation
    void
    run()
    { @_74_
        // Make sure we run on the strand
        if(! stream_.get_executor().running_in_this_thread())
            return net::post(
                stream_.get_executor(),
                std::bind(
                    &ssl_http_session::run,
                    shared_from_this()));

        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        // Note, this is the buffered version of the handshake.
        stream_.async_handshake(
            ssl::stream_base::server,
            buffer_.data(),
            std::bind(
                &ssl_http_session::on_handshake,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2));
    } @_74_
    void
    on_handshake(
        beast::error_code ec,
        std::size_t bytes_used)
    { @_75_
        if(ec)
            return fail(ec, "handshake");

        // Consume the portion of the buffer used by the handshake
        buffer_.consume(bytes_used);

        do_read();
    } @_75_

    void
    do_eof()
    { @_76_
        eof_ = true;

        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        stream_.async_shutdown(
            std::bind(
                &ssl_http_session::on_shutdown,
                shared_from_this(),
                std::placeholders::_1));
    } @_76_

    void
    on_shutdown(beast::error_code ec)
    { @_77_
        // Happens when the shutdown times out
        if(ec == net::error::operation_aborted)
            return;

        if(ec)
            return fail(ec, "shutdown");

        // At this point the connection is closed gracefully
    } @_77_
}; @_70_

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{ @_78_
    beast::tcp_stream<net::io_context::strand> stream_;
    ssl::context& ctx_;
    std::shared_ptr<std::string const> doc_root_;
    beast::flat_buffer buffer_;

public:
    explicit
    detect_session(
        tcp::socket socket,
        ssl::context& ctx,
        std::shared_ptr<std::string const> const& doc_root)
        : stream_(std::move(socket))
        , ctx_(ctx)
        , doc_root_(doc_root)
    { @_79_
    } @_79_

    // Launch the detector
    void
    run()
    { @_80_
        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        async_detect_ssl(
            stream_,
            buffer_,
            std::bind(
                &detect_session::on_detect,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2));
    } @_80_

    void
    on_detect(beast::error_code ec, boost::tribool result)
    { @_81_
        if(ec)
            return fail(ec, "detect");

        if(result)
        { @_82_
            // Launch SSL session
            std::make_shared<ssl_http_session>(
                std::move(stream_),
                ctx_,
                std::move(buffer_),
                doc_root_)->run();
            return;
        } @_82_

        // Launch plain session
        std::make_shared<plain_http_session>(
            std::move(stream_),
            std::move(buffer_),
            doc_root_)->run();
    } @_81_
}; @_78_

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{ @_83_
    ssl::context& ctx_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> doc_root_;

public:
    listener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint,
        std::shared_ptr<std::string const> const& doc_root)
        : ctx_(ctx)
        , acceptor_(ioc)
        , doc_root_(doc_root)
    { @_84_
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if(ec)
        { @_85_
            fail(ec, "open");
            return;
        } @_85_

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec)
        { @_86_
            fail(ec, "set_option");
            return;
        } @_86_

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if(ec)
        { @_87_
            fail(ec, "bind");
            return;
        } @_87_

        // Start listening for connections
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if(ec)
        { @_88_
            fail(ec, "listen");
            return;
        } @_88_
    } @_84_

    // Start accepting incoming connections
    void
    run()
    { @_89_
        if(! acceptor_.is_open())
            return;
        do_accept();
    } @_89_

    void
    do_accept()
    { @_90_
        acceptor_.async_accept(
            std::bind(
                &listener::on_accept,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2));
    } @_90_

    void
    on_accept(beast::error_code ec, tcp::socket socket)
    { @_91_
        if(ec)
        { @_92_
            fail(ec, "accept");
        } @_92_
        else
        { @_93_
            // Create the detector http_session and run it
            std::make_shared<detect_session>(
                std::move(socket),
                ctx_,
                doc_root_)->run();
        } @_93_

        // Accept another connection
        do_accept();
    } @_91_
}; @_83_

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{ @_94_
    // Check command line arguments.
    if (argc != 5)
    { @_95_
        std::cerr <<
            "Usage: advanced-server-flex <address> <port> <doc_root> <threads>\n" <<
            "Example:\n" <<
            "    advanced-server-flex 0.0.0.0 8080 . 1\n";
        return EXIT_FAILURE;
    } @_95_
    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const doc_root = std::make_shared<std::string>(argv[3]);
    auto const threads = std::max<int>(1, std::atoi(argv[4]));

    // The io_context is required for all I/O
    net::io_context ioc{threads};

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::sslv23};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    // Create and launch a listening port
    std::make_shared<listener>(
        ioc,
        ctx,
        tcp::endpoint{address, port},
        doc_root)->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](beast::error_code const&, int)
        { @_96_
            // Stop the `io_context`. This will cause `run()`
            // to return immediately, eventually destroying the
            // `io_context` and all of the sockets in it.
            ioc.stop();
        }); @_96_

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ioc]
        { @_97_
            ioc.run();
        }); @_97_
    ioc.run();

    // (If we get here, it means we got a SIGINT or SIGTERM)

    // Block until all the threads exit
    for(auto& t : v)
        t.join();

    return EXIT_SUCCESS;
} @_94_
