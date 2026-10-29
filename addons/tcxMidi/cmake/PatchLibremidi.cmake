# Local patch for the SHA-pinned libremidi 5.4.3. No git or patch executable needed.
# Input and ALSA output hashes are checked before any write; VERIFY_ONLY never modifies sources.
# file(WRITE) uses native text mode, producing CRLF on Windows. CONFIGURE with
# NEWLINE_STYLE UNIX writes LF in binary mode (available since CMake 3.18).
# @ONLY preserves ${...}; these hash-pinned sources contain no @VAR@ references
# or #cmakedefine directives that CONFIGURE could expand.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED LIBREMIDI_SOURCE_DIR)
    message(FATAL_ERROR "LIBREMIDI_SOURCE_DIR is required")
endif()
set(input_files
    include/libremidi/detail/midi_stream_decoder.hpp
    include/libremidi/input_configuration.hpp
    include/libremidi/backends/alsa_seq/midi_in.hpp
    include/libremidi/backends/winmm/midi_in.hpp)
set(input_hashes
    912892cedd53914c7bd8d936fb1d3f9ad8196c37de1697789f8254172434df99
    087a3a04031b017999607fa49a7006fef330410a1c9297260ac25fdb653f8d8c
    9c2f511b98f13a63cbb5f797fe16142f546391d63b9cf431c79f9f9b1ec0a537
    c57134c237f4d36bfa0a8b0dce0c7d79f07f959417b0e22bd7a422e6c4c663a5)
foreach(input expected IN ZIP_LISTS input_files input_hashes)
    set(actual "")
    if(EXISTS "${LIBREMIDI_SOURCE_DIR}/${input}")
        file(SHA256 "${LIBREMIDI_SOURCE_DIR}/${input}" actual)
    endif()
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "tcxMidi libremidi input differs from upstream: ${LIBREMIDI_SOURCE_DIR}/${input}. Use clean v5.4.3 sources, then apply PatchLibremidi.cmake for R4 only; replace the stale source override/cache.")
    endif()
endforeach()
set(path0 "${LIBREMIDI_SOURCE_DIR}/include/libremidi/backends/alsa_seq/midi_out.hpp")
file(SHA256 "${path0}" hash0)
set(original0 "c636311563779e5fe9e566be9d56ed0d2052d97d06286de7f8456791ea0cc269")
set(patched0 "0590ffc031e6fbc2076af581542095c057bedbc6766d137bd14114eb4ec9525b")
if(NOT hash0 STREQUAL patched0)
    if(VERIFY_ONLY OR NOT hash0 STREQUAL original0)
        message(FATAL_ERROR "tcxMidi requires its checked libremidi 5.4.3 ALSA SysEx output patch: ${path0}. Patch a private source copy with cmake -DLIBREMIDI_SOURCE_DIR=<copy> -P ${CMAKE_CURRENT_LIST_FILE}; do not use an unpatched source override/cache.")
    endif()
endif()
if(VERIFY_ONLY)
    return()
endif()
if(hash0 STREQUAL original0)
    file(READ "${path0}" contents)
    string(REPLACE [==[#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

NAMESPACE_LIBREMIDI::alsa_seq
{

class midi_out_impl final
    : public midi1::out_api
    , private alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , alsa_seq::output_configuration
  {
  } configuration;

  midi_out_impl(libremidi::output_configuration&& conf, alsa_seq::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (init_client(configuration) < 0)
    {
      libremidi_handle_error(
          this->configuration,
          "error creating ALSA sequencer client "
          "object.");
      return;
    }

    if (snd.midi.event_new(this->m_bufferSize, &this->coder) < 0)
    {
      libremidi_handle_error(this->configuration, "error initializing MIDI event parser.");
      return;
    }
    snd.midi.event_init(this->coder);

    this->client_open_ = stdx::error{};
  }

  ~midi_out_impl() override
  {
    // Close a connection if it exists.
    midi_out_impl::close_port();

    // Cleanup.
    if (this->vport >= 0)
      snd.seq.delete_port(this->seq, this->vport);
    if (this->coder)
      snd.midi.event_free(this->coder);

    if (!configuration.context)
      snd.seq.close(this->seq);

    client_open_ = std::errc::not_connected;
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_SEQ; }

  [[nodiscard]] int create_port(std::string_view portName)
  {
    return alsa_data::create_port(
        *this, portName, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION, std::nullopt);
  }

  stdx::error open_port(const output_port& p, std::string_view portName) override
  {
    unsigned int n_src
        = this->get_port_count(SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
    if (n_src < 1)
    {
      libremidi_handle_error(this->configuration, "no MIDI output sources found!");
      return make_error_code(std::errc::no_such_device);
    }

    auto sink = get_port_info(p);
    if (!sink)
      return std::errc::invalid_argument;

    if (int err = create_port(portName); err < 0)
    {
      libremidi_handle_error(configuration, "ALSA error creating port.");
      return from_errc(err);
    }

    snd_seq_addr_t source{
        .client = (unsigned char)snd.seq.client_id(this->seq), .port = (unsigned char)this->vport};
    if (int err = create_connection(*this, source, *sink, true); err < 0)
    {
      libremidi_handle_error(configuration, "ALSA error making port connection.");
      return from_errc(err);
    }

    return stdx::error{};
  }

  stdx::error open_virtual_port(std::string_view portName) override
  {
    if (int err = create_port(portName); err < 0)
      return from_errc(err);
    return stdx::error{};
  }

  stdx::error close_port() override
  {
    unsubscribe();
    return stdx::error{};
  }

  stdx::error set_port_name(std::string_view portName) override
  {
    return alsa_data::set_port_name(portName);
  }

  stdx::error send_message(const unsigned char* message, std::size_t size) override
  {
    int64_t result{};
    if (size > this->m_bufferSize)
    {
      this->m_bufferSize = size;
      result = snd.midi.event_resize_buffer(this->coder, size);
      if (result != 0)
      {
        libremidi_handle_error(
            this->configuration,
            "ALSA error resizing MIDI event "
            "buffer.");
        return std::errc::no_buffer_space;
      }
    }

    std::size_t offset = 0;
    while (offset < size)
    {
      snd_seq_event_t ev;
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_source(&ev, this->vport);
      snd_seq_ev_set_subs(&ev);
      // FIXME direct is set but snd_seq_event_output_direct is not used...
      snd_seq_ev_set_direct(&ev);

      const int64_t n_bytes = size; // signed to avoir potential overflow with size - offset below
      result = snd.midi.event_encode(this->coder, message + offset, (long)(n_bytes - offset), &ev);
      if (result < 0)
      {
        libremidi_handle_warning(this->configuration, "event parsing error!");
        return std::errc::bad_message;
      }

      if (ev.type == SND_SEQ_EVENT_NONE)
      {
        libremidi_handle_warning(this->configuration, "incomplete message!");
        return std::errc::message_size;
      }

      offset += result;

      result = snd.seq.event_output(this->seq, &ev);
      if (result < 0)
      {
        libremidi_handle_warning(this->configuration, "error sending MIDI message to port.");
        return std::errc::io_error;
      }
    }
    snd.seq.drain_output(this->seq);
    return stdx::error{};
  }

private:
  uint64_t m_bufferSize{32};
};
}
]==] [==[#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

#include <thread>

NAMESPACE_LIBREMIDI::alsa_seq
{

class midi_out_impl final
    : public midi1::out_api
    , private alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , alsa_seq::output_configuration
  {
  } configuration;

  midi_out_impl(libremidi::output_configuration&& conf, alsa_seq::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (init_client(configuration) < 0)
    {
      libremidi_handle_error(
          this->configuration,
          "error creating ALSA sequencer client "
          "object.");
      return;
    }

    if (snd.midi.event_new(this->m_bufferSize, &this->coder) < 0)
    {
      libremidi_handle_error(this->configuration, "error initializing MIDI event parser.");
      return;
    }
    snd.midi.event_init(this->coder);

    if (!drop_output)
      return;

    this->client_open_ = stdx::error{};
  }

  ~midi_out_impl() override
  {
    // Close a connection if it exists.
    midi_out_impl::close_port();

    // Cleanup.
    if (this->vport >= 0)
      snd.seq.delete_port(this->seq, this->vport);
    if (this->coder)
      snd.midi.event_free(this->coder);

    if (!configuration.context)
      snd.seq.close(this->seq);

    client_open_ = std::errc::not_connected;
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_SEQ; }

  [[nodiscard]] int create_port(std::string_view portName)
  {
    return alsa_data::create_port(
        *this, portName, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION, std::nullopt);
  }

  stdx::error open_port(const output_port& p, std::string_view portName) override
  {
    unsigned int n_src
        = this->get_port_count(SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
    if (n_src < 1)
    {
      libremidi_handle_error(this->configuration, "no MIDI output sources found!");
      return make_error_code(std::errc::no_such_device);
    }

    auto sink = get_port_info(p);
    if (!sink)
      return std::errc::invalid_argument;

    if (int err = create_port(portName); err < 0)
    {
      libremidi_handle_error(configuration, "ALSA error creating port.");
      return from_errc(err);
    }

    snd_seq_addr_t source{
        .client = (unsigned char)snd.seq.client_id(this->seq), .port = (unsigned char)this->vport};
    if (int err = create_connection(*this, source, *sink, true); err < 0)
    {
      libremidi_handle_error(configuration, "ALSA error making port connection.");
      return from_errc(err);
    }

    return stdx::error{};
  }

  stdx::error open_virtual_port(std::string_view portName) override
  {
    if (int err = create_port(portName); err < 0)
      return from_errc(err);
    return stdx::error{};
  }

  stdx::error close_port() override
  {
    unsubscribe();
    return stdx::error{};
  }

  stdx::error set_port_name(std::string_view portName) override
  {
    return alsa_data::set_port_name(portName);
  }

  stdx::error send_message(const unsigned char* message, std::size_t size) override
  {
    int64_t result{};
    // Keep the encoder fixed: ALSA emits partial SysEx events at this size.
    // Resizing to the whole dump creates one event larger than the seq buffers.
    std::size_t offset = 0;
    while (offset < size)
    {
      snd_seq_event_t ev;
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_source(&ev, this->vport);
      snd_seq_ev_set_subs(&ev);
      // Queue-direct delivery occurs synchronously when the buffer is drained.
      snd_seq_ev_set_direct(&ev);

      // The codec's length is long, even when size_t is wider.
      const auto remaining = std::min<std::size_t>(size - offset, this->m_bufferSize);
      result = snd.midi.event_encode(this->coder, message + offset, (long)remaining, &ev);
      if (result <= 0)
      {
        libremidi_handle_warning(this->configuration, "event parsing error!");
        return std::errc::bad_message;
      }

      if (ev.type == SND_SEQ_EVENT_NONE)
      {
        libremidi_handle_warning(this->configuration, "incomplete message!");
        return std::errc::message_size;
      }

      offset += result;

      result = snd.seq.event_output(this->seq, &ev);
      if (result < 0)
      {
        drop_output(this->seq);
        return from_errc(static_cast<int>(result));
      }

      // Bound (driver wait): at most 1000 EAGAIN/EINTR retries, separated by 1 ms; exhaustion drops pending bytes and reports timed_out.
      // A supplied blocking handle can block in the kernel drain beyond this
      // retry bound. A prefix may already be delivered; stop after a failure.
      for (int attempt = 0;; ++attempt)
      {
        result = snd.seq.drain_output(this->seq);
        if (result >= 0)
          break;
        if (result != -EAGAIN && result != -EINTR)
        {
          drop_output(this->seq);
          return from_errc(static_cast<int>(result));
        }
        if (attempt == 1000)
        {
          drop_output(this->seq);
          return std::errc::timed_out;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      // Pace long SysEx at <=256 bytes/ms (virtual loopback, not DIN wire
      // speed), giving a reader time to release its ~200 default pool cells.
      // Receiver overflow is a receive-side loss, not a send acknowledgement.
      if (ev.type == SND_SEQ_EVENT_SYSEX && offset < size)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return stdx::error{};
  }

private:
  // ALSA's default userspace output buffer is one page (normally 4096 bytes).
  // A 256-byte payload plus event header fits it and a 200-cell input pool
  // comfortably; this is an event chunk size, never a logical message limit.
  // Bound (sequencer buffers): encode at most 256 bytes per event; larger messages continue in subsequent chunks.
  static constexpr std::size_t m_bufferSize{256};
  decltype(&::snd_seq_drop_output) drop_output
      = snd.library.symbol<decltype(drop_output)>("snd_seq_drop_output");
};
}
]==] contents "${contents}")
    string(SHA256 result "${contents}")
    if(NOT result STREQUAL patched0)
        message(FATAL_ERROR "libremidi patch result mismatch: ${path0}")
    endif()
    file(CONFIGURE OUTPUT "${path0}" CONTENT "${contents}" @ONLY NEWLINE_STYLE UNIX)
endif()
