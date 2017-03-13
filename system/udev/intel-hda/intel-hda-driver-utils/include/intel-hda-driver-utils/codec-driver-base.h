// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <intel-hda-driver-utils/codec-commands.h>
#include <intel-hda-driver-utils/driver-channel.h>
#include <intel-hda-driver-utils/stream-base.h>
#include <magenta/device/intel-hda.h>
#include <mx/handle.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_ptr.h>

class IntelHDAStreamBase;

class IntelHDACodecDriverBase : public DriverChannel::Owner {
public:
    virtual void Shutdown();

protected:
    static constexpr uint32_t CODEC_TID = 0xFFFFFFFF;

    virtual ~IntelHDACodecDriverBase() { }

    ///////////////////////////////////////////////////////////////////////////
    //
    // Methods used by derived classes in order to implement their driver.
    //
    ///////////////////////////////////////////////////////////////////////////

    // Bind should only ever be called exactly once (during driver
    // instantiation).  Drivers must make sure that no other methods are in
    // flight during a call to Bind.
    mx_status_t Bind(mx_driver_t* driver, mx_device_t* codec_dev);

    // Send a codec command to our codec device.
    mx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, bool no_ack);

    virtual mx_status_t Start() { return NO_ERROR; }
    virtual mx_status_t ProcessUnsolicitedResponse(const CodecResponse& resp) { return NO_ERROR; }
    virtual mx_status_t ProcessSolicitedResponse  (const CodecResponse& resp) { return NO_ERROR; }

    // DriverChannel::Owner implementation
    mx_status_t ProcessChannel(DriverChannel& channel,
                               const mx_io_packet_t& io_packet) final;
    void NotifyChannelDeactivated(const DriverChannel& channel) final;

    mxtl::RefPtr<IntelHDAStreamBase> GetActiveStream(uint32_t stream_id)
        TA_EXCL(active_streams_lock_);
    mx_status_t ActivateStream(const mxtl::RefPtr<IntelHDAStreamBase>& stream)
        TA_EXCL(active_streams_lock_);
    mx_status_t DeactivateStream(uint32_t stream_id)
        TA_EXCL(active_streams_lock_);

    // Debug logging
    virtual void PrintDebugPrefix() const;

private:
    friend class mxtl::RefPtr<IntelHDACodecDriverBase>;

    union CodecChannelResponses {
        ihda_cmd_hdr_t    hdr;
        SendCORBCmdResp   send_corb;
        RequestStreamResp request_stream;
        SetStreamFmtResp  set_stream_fmt;
    };

    // Called in order to unlink this device from the controller driver.  After
    // this call returns, the codec driver is guaranteed that no calls to any of
    // the driver implemented callbacks (see below) are in flight, and that no
    // new calls will be initiated.  It is not safe to make this call during a
    // controller callback.  To unlink from a controller during a callback,
    // return an error code from the callback.
    void UnlinkFromController();

    mx_status_t ProcessStreamResponse(const mxtl::RefPtr<IntelHDAStreamBase>& stream,
                                      const CodecChannelResponses& resp,
                                      uint32_t resp_size,
                                      mx::handle&& rxed_handle);

    mx_driver_t* codec_driver_ = nullptr;
    mx_device_t* codec_device_ = nullptr;

    mxtl::Mutex device_channel_lock_;
    mxtl::RefPtr<DriverChannel> device_channel_ TA_GUARDED(device_channel_lock_);

    using ActiveStreams = mxtl::WAVLTree<uint32_t, mxtl::RefPtr<IntelHDAStreamBase>>;
    mxtl::Mutex   active_streams_lock_;
    ActiveStreams active_streams_ TA_GUARDED(active_streams_lock_);

    mxtl::Mutex shutdown_lock_ TA_ACQ_BEFORE(device_channel_lock_, active_streams_lock_);
    bool        shutting_down_ TA_GUARDED(shutdown_lock_) = false;
};