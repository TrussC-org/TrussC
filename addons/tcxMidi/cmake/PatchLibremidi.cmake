# Local patch for the SHA-pinned libremidi 5.4.3. No git or patch executable needed.
# All input hashes are checked before any write; VERIFY_ONLY never modifies sources.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED LIBREMIDI_SOURCE_DIR)
    message(FATAL_ERROR "LIBREMIDI_SOURCE_DIR is required")
endif()
set(path0 "${LIBREMIDI_SOURCE_DIR}/include/libremidi/detail/midi_stream_decoder.hpp")
file(SHA256 "${path0}" hash0)
set(original0 "912892cedd53914c7bd8d936fb1d3f9ad8196c37de1697789f8254172434df99")
set(patched0 "09610268e4978dac23a10f54f88e7540dcb03666c46c5f1f0d69759f710d0b14")
if(NOT hash0 STREQUAL patched0)
    if(VERIFY_ONLY OR NOT hash0 STREQUAL original0)
        message(FATAL_ERROR "tcxMidi requires its checked libremidi 5.4.3 SysEx patch: ${path0}. Patch a private source copy with cmake -DLIBREMIDI_SOURCE_DIR=<copy> -P ${CMAKE_CURRENT_LIST_FILE}; do not use an unpatched source override/cache.")
    endif()
endif()
set(path1 "${LIBREMIDI_SOURCE_DIR}/include/libremidi/input_configuration.hpp")
file(SHA256 "${path1}" hash1)
set(original1 "087a3a04031b017999607fa49a7006fef330410a1c9297260ac25fdb653f8d8c")
set(patched1 "91891013773211537e9397e4cd4ac994fe3cb400b0eea80f19dc8f5575d426ba")
if(NOT hash1 STREQUAL patched1)
    if(VERIFY_ONLY OR NOT hash1 STREQUAL original1)
        message(FATAL_ERROR "tcxMidi requires its checked libremidi 5.4.3 SysEx patch: ${path1}. Patch a private source copy with cmake -DLIBREMIDI_SOURCE_DIR=<copy> -P ${CMAKE_CURRENT_LIST_FILE}; do not use an unpatched source override/cache.")
    endif()
endif()
set(path2 "${LIBREMIDI_SOURCE_DIR}/include/libremidi/backends/alsa_seq/midi_in.hpp")
file(SHA256 "${path2}" hash2)
set(original2 "9c2f511b98f13a63cbb5f797fe16142f546391d63b9cf431c79f9f9b1ec0a537")
set(patched2 "316f03a84f76c59a2629b863d656789b0af9821500fe4ce852e038417d6e895d")
if(NOT hash2 STREQUAL patched2)
    if(VERIFY_ONLY OR NOT hash2 STREQUAL original2)
        message(FATAL_ERROR "tcxMidi requires its checked libremidi 5.4.3 SysEx patch: ${path2}. Patch a private source copy with cmake -DLIBREMIDI_SOURCE_DIR=<copy> -P ${CMAKE_CURRENT_LIST_FILE}; do not use an unpatched source override/cache.")
    endif()
endif()
if(VERIFY_ONLY)
    return()
endif()
if(hash0 STREQUAL original0)
    file(READ "${path0}" contents)
    string(REPLACE [==[struct input_state_machine : input_state_machine_base<input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  void reset()
  {
    message.bytes.clear();
    message.timestamp = {};
    m_state = main;
  }

  bool has_finished_sysex(std::span<const uint8_t> bytes) const noexcept
  {
    return (((bytes.front() == 0xF0) || (m_state == in_sysex)) && (bytes.back() == 0xF7));
  }

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_multi_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

  // Function to process bytes corresponding to at most one midi event
  // e.g. a midi channel event or a single sysex
  void on_bytes(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

private:
  void on_bytes_multi_segmented(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    int64_t n_bytes = bytes.size();
    int64_t i_byte = 0;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (m_state)
    {
      case in_sysex: {
        return on_continue_sysex(cb, bytes, finished_sysex);
      }
      case main: {
        while (i_byte < n_bytes)
        {
          int64_t size = 1;
          // We are expecting that the next byte in the packet is a status
          // byte.
          const auto status = bytes[i_byte];
          if (!(status & 0x80))
            break;

          // Determine the number of bytes in the MIDI message.
          if (status < 0xC0)
            size = 3;
          else if (status < 0xE0)
            size = 2;
          else if (status < 0xF0)
            size = 3;
          else if (status == 0xF0)
          {
            if (configuration.ignore_sysex)
            {
              size = 0;
              i_byte = n_bytes;
            }
            else
            {
              size = n_bytes - i_byte;
            }

            if (bytes[n_bytes - 1] != 0xF7)
            {
              // We know per CoreMIDI API there can't be anything else in this packet
              m_state = in_sysex;
              message.assign(bytes.begin(), bytes.begin() + size);
              message.timestamp = timestamp;
              return;
            }
          }
          else if (status == 0xF1)
          {
            // A MIDI time code message
            if (configuration.ignore_timing)
            {
              size = 0;
              i_byte += 2;
            }
            else
            {
              size = 2;
            }
          }
          else if (status == 0xF2)
            size = 3;
          else if (status == 0xF3)
            size = 2;
          else if (status == 0xF8)
          {
            // A MIDI timing tick message
            if (configuration.ignore_timing)
            {
              size = 0;
              i_byte += 1;
            }
            else
            {
              size = 1;
            }
          }
          else if (status == 0xFE)
          {
            // A MIDI active sensing message
            if (configuration.ignore_sensing)
            {
              size = 0;
              i_byte += 1;
            }
            else
            {
              size = 1;
            }
          }
          else
          {
            // Remaining real-time messages
            size = 1;
          }

          // Now process the actual bytes of the message
          if (size > 0)
          {
            auto begin = bytes.begin() + i_byte;
            message.assign(begin, begin + size);
            message.timestamp = timestamp;

            cb(std::move(message));
            message.clear();

            i_byte += size;
          }
        }
      }
    }
  }

  void on_continue_sysex(
      const message_callback& cb, std::span<const uint8_t> bytes, bool finished_sysex)
  {
    if (finished_sysex)
      m_state = main;

    if (configuration.ignore_sysex)
    {
      return;
    }
    else
    {
      message.insert(message.end(), bytes.begin(), bytes.end());
      if (finished_sysex)
      {
        cb(std::move(message));
        message.clear();
      }
    }
    return;
  }

  void on_main(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp,
      bool finished_sysex)
  {
    switch (bytes[0])
    {
      // SYSEX start
      case 0xF0: {
        if (!finished_sysex)
          m_state = in_sysex;

        if (!this->configuration.ignore_sysex)
        {
          message.assign(bytes.begin(), bytes.end());
          message.timestamp = timestamp;
          if (finished_sysex)
          {
            cb(std::move(message));
            message.clear();
          }
        }

        return;
      }

      case 0xF1:
      case 0xF8:
        if (this->configuration.ignore_timing)
          return;
        break;

      case 0xFE:
        if (this->configuration.ignore_sensing)
          return;
        break;

      default:
        break;
    }

    message.assign(bytes.begin(), bytes.end());
    message.timestamp = timestamp;

    cb(std::move(message));
    message.clear();
  }

  void
  on_bytes_segmented(const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (bytes.empty())
      return;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (m_state)
    {
      case in_sysex:
        return on_continue_sysex(cb, bytes, finished_sysex);

      case main:
        return on_main(cb, bytes, timestamp, finished_sysex);
    }
  }

public:
  libremidi::message message;

private:
  enum
  {
    main,
    in_sysex
  } m_state{main};
};
}
]==] [==[struct input_state_machine : input_state_machine_base<input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  void reset()
  {
    message.bytes.clear();
    message.timestamp = {};
    m_state = main;
    short_size = short_needed = running_status = 0;
  }

  // Bound (security): device input must not grow receiver memory without limit. Overflow discards whole SysEx and is counted.
  // The 1 MiB policy includes F0/F7; it is not a MIDI protocol maximum.
  static constexpr std::size_t sysex_max_bytes = 1'048'576;

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_multi_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

  // Function to process bytes corresponding to at most one midi event
  // e.g. a midi channel event or a single sysex
  void on_bytes(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

private:
  void emit_realtime(const message_callback& cb, uint8_t status, int64_t timestamp)
  {
    if ((status == 0xF8 && configuration.ignore_timing)
        || (status == 0xFE && configuration.ignore_sensing))
      return;
    libremidi::message realtime{status};
    realtime.timestamp = timestamp;
    cb(std::move(realtime));
  }

  void on_bytes_multi_segmented(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    while (!bytes.empty())
    {
      if (m_state != main || bytes.front() == 0xF0)
      {
        short_size = short_needed = running_status = 0;
        bytes = on_sysex(cb, bytes, timestamp);
        continue;
      }
      const auto byte = bytes.front();
      bytes = bytes.subspan(1);
      if (byte >= 0xF8)
      {
        emit_realtime(cb, byte, timestamp);
        continue;
      }
      if (byte & 0x80)
      {
        // A new status replaces any incomplete short message. System common
        // statuses cancel running status; real-time bytes above preserve it.
        short_bytes[0] = byte;
        short_size = 1;
        short_needed = byte < 0xF0 ? (byte >= 0xC0 && byte < 0xE0 ? 2 : 3)
                       : byte == 0xF2 ? 3 : (byte == 0xF1 || byte == 0xF3 ? 2 : 1);
        running_status = byte < 0xF0 ? byte : 0;
        short_timestamp = timestamp;
      }
      else
      {
        if (short_size == 0)
        {
          if (!running_status)
            continue;
          short_bytes[0] = running_status;
          short_size = 1;
          short_timestamp = timestamp;
        }
        short_bytes[short_size++] = byte;
      }
      if (short_size == short_needed)
      {
        // Keep partial channel/running-status state across input chunks.
        if (!(short_bytes[0] == 0xF1 && configuration.ignore_timing))
        {
          message.assign(short_bytes, short_bytes + short_size);
          message.timestamp = short_timestamp;
          cb(std::move(message));
          message.clear();
        }
        short_size = 0;
      }
    }
  }

  void reject_sysex(std::size_t needed)
  {
    message.clear();
    short_size = short_needed = running_status = 0;
    m_state = discard_sysex;
    if (configuration.on_sysex_overflow)
      configuration.on_sysex_overflow(needed);
  }

  void append_sysex(std::span<const uint8_t> bytes)
  {
    if (configuration.ignore_sysex || m_state == discard_sysex)
      return;

    // Bound (security): admit untrusted initial SysEx before assign. Overflow discards whole SysEx and is counted.
    if (message.empty() && bytes.size() > sysex_max_bytes)
      return reject_sysex(bytes.size());
    // Bound (security): admit cumulative device bytes before insert. Overflow discards whole SysEx and is counted.
    if (!message.empty() && bytes.size() > sysex_max_bytes - message.size())
      return reject_sysex(message.size() + bytes.size());

    const auto needed = message.size() + bytes.size();
    // Bound (security): constrain geometric allocation from device bytes. Overflow discards whole SysEx and is counted.
    if (needed > message.bytes.capacity())
      message.bytes.reserve(std::min(sysex_max_bytes,
          std::max(needed, message.bytes.capacity() * 2)));
    if (message.empty())
      message.assign(bytes.begin(), bytes.end());
    else
      message.insert(message.end(), bytes.begin(), bytes.end());
  }

  std::span<const uint8_t> on_sysex(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    std::size_t pos = 0;
    if (m_state == main)
    {
      message.clear();
      message.timestamp = timestamp;
      m_state = in_sysex;
      pos = 1; // Include the initial F0 in the first span.
    }

    std::size_t start = 0;
    while (pos < bytes.size())
    {
      const auto status = bytes[pos];
      if (status < 0x80)
      {
        ++pos;
        continue;
      }
      if (status == 0xF7)
      {
        append_sysex(bytes.subspan(start, pos + 1 - start));
        if (m_state != discard_sysex && !configuration.ignore_sysex)
          cb(std::move(message));
        message.clear();
        m_state = main;
        return bytes.subspan(pos + 1);
      }

      append_sysex(bytes.subspan(start, pos - start));
      if (status >= 0xF8)
      {
        // Real-time messages never terminate SysEx, including while discarding.
        emit_realtime(cb, status, timestamp);
        start = ++pos;
      }
      else
      {
        // A non-real-time status terminates the old SysEx; reprocess the status.
        message.clear();
        m_state = main;
        return bytes.subspan(pos);
      }
    }
    append_sysex(bytes.subspan(start));
    return {};
  }

  void
  on_bytes_segmented(const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    on_bytes_multi_segmented(cb, bytes, timestamp);
  }

public:
  libremidi::message message;

private:
  uint8_t short_bytes[3]{};
  uint8_t short_size{}, short_needed{}, running_status{};
  int64_t short_timestamp{};
  enum
  {
    main,
    in_sysex,
    discard_sysex
  } m_state{main};
};
}
]==] contents "${contents}")
    string(SHA256 result "${contents}")
    if(NOT result STREQUAL patched0)
        message(FATAL_ERROR "libremidi patch result mismatch: ${path0}")
    endif()
    file(WRITE "${path0}" "${contents}")
endif()
if(hash1 STREQUAL original1)
    file(READ "${path1}" contents)
    string(REPLACE [==[  raw_callback on_raw_data{};

  //! Set a custom callback function to be invoked for timestamping MIDI messages.
  //! Input: the API provided timestamp in nanoseconds, if available, for reference.
  //! (e.g. the same as "Absolute").
  //! Mandatory if timestamps == timestamp_mode::Custom, unused otherwise.
  timestamp_callback get_timestamp{};

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  midi_error_callback on_error{};
  midi_warning_callback on_warning{};

  //! Specify whether certain MIDI message types should be queued or ignored
  //! during input.
  /*!
]==] [==[  raw_callback on_raw_data{};

  //! Set a custom callback function to be invoked for timestamping MIDI messages.
  //! Input: the API provided timestamp in nanoseconds, if available, for reference.
  //! (e.g. the same as "Absolute").
  //! Mandatory if timestamps == timestamp_mode::Custom, unused otherwise.
  timestamp_callback get_timestamp{};

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  midi_error_callback on_error{};
  midi_warning_callback on_warning{};

  //! TrussC: one callback per discarded SysEx, on the input thread. No logging here.
  std::function<void(std::size_t needed_bytes)> on_sysex_overflow{};

  //! Specify whether certain MIDI message types should be queued or ignored
  //! during input.
  /*!
]==] contents "${contents}")
    string(SHA256 result "${contents}")
    if(NOT result STREQUAL patched1)
        message(FATAL_ERROR "libremidi patch result mismatch: ${path1}")
    endif()
    file(WRITE "${path1}" "${contents}")
endif()
if(hash2 STREQUAL original2)
    file(READ "${path2}" contents)
    string(REPLACE [==[  {
    if constexpr (ConfigurationImpl::midi_version == 1)
    {
      switch (ev.type)
      {
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
          return 0;
        case SND_SEQ_EVENT_SYSEX: {
          if (configuration.ignore_sysex)
            return 0;
          else if (ev.data.ext.len > decoding_buffer.size())
            decoding_buffer.resize(ev.data.ext.len);
          break;
        }
      }

      static constexpr timestamp_backend_info timestamp_info{
          .has_absolute_timestamps = true,
          .absolute_is_monotonic = false,
]==] [==[  {
    if constexpr (ConfigurationImpl::midi_version == 1)
    {
      static constexpr timestamp_backend_info timestamp_info{
          .has_absolute_timestamps = true,
          .absolute_is_monotonic = false,
]==] contents "${contents}")
    string(REPLACE [==[      const auto to_ns = [ts = ev.time.time] {
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + static_cast<int64_t>(ts.tv_nsec);
      };
      auto buf = decoding_buffer.data();
      auto buf_space = decoding_buffer.size();

]==] [==[      const auto to_ns = [ts = ev.time.time] {
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + static_cast<int64_t>(ts.tv_nsec);
      };
      switch (ev.type)
      {
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
          return 0;
        case SND_SEQ_EVENT_SYSEX: {
          if (configuration.ignore_sysex)
            return 0;
          // Bound (security): borrow SysEx bytes without growing codec scratch.
          // The shared decoder admits each logical message before storage growth.
          m_processing.on_bytes(
              {static_cast<const uint8_t*>(ev.data.ext.ptr), ev.data.ext.len},
              m_processing.template timestamp<timestamp_info>(to_ns, 0));
          return 0;
        }
      }

      auto buf = decoding_buffer.data();
      auto buf_space = decoding_buffer.size();

]==] contents "${contents}")
    string(SHA256 result "${contents}")
    if(NOT result STREQUAL patched2)
        message(FATAL_ERROR "libremidi patch result mismatch: ${path2}")
    endif()
    file(WRITE "${path2}" "${contents}")
endif()
