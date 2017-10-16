// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#include <librealsense2/rs.hpp>

#include "device_hub.h"


namespace librealsense
{
    typedef rs2::devices_changed_callback<std::function<void(rs2::event_information& info)>> hub_devices_changed_callback;

    std::vector<std::shared_ptr<device_info>> filter_by_vid(std::vector<std::shared_ptr<device_info>> devices , int vid)
    {
        std::vector<std::shared_ptr<device_info>> result;
        for (auto dev : devices)
        {
            auto data = dev->get_device_data();
            for (auto uvc : data.uvc_devices)
            {
                if (uvc.vid == vid || vid == 0)
                {
                    result.push_back(dev);
                    break;
                }
            }
        }
        return result;
    }

    device_hub::device_hub(std::shared_ptr<librealsense::context> ctx, int vid,
                           bool register_device_notifications)
        : _ctx(ctx), _vid(vid),
          _register_device_notifications(register_device_notifications)
    {
        _device_list = filter_by_vid(_ctx->query_devices(), _vid);

        auto cb = new hub_devices_changed_callback([&](rs2::event_information& info)
                   {
                        std::unique_lock<std::mutex> lock(_mutex);

                        _device_list = filter_by_vid(_ctx->query_devices(), _vid);

                        // Current device will point to the first available device
                        _camera_index = 0;
                        if (_device_list.size() > 0)
                        {
                           _cv.notify_all();
                        }
                    });

        _ctx->set_devices_changed_callback({cb,  [](rs2_devices_changed_callback* p) { p->release(); }});
    }

    std::shared_ptr<device_interface> device_hub::create_device(const std::string& serial, bool cycle_devices)
    {
        std::shared_ptr<device_interface> res = nullptr;
        for(auto i = 0; ((i< _device_list.size()) && (nullptr == res)); i++)
        {
            // user can switch the devices by calling to wait_for_device until he get the desire device
            // _camera_index is the curr device that user want to work with

            auto d = _device_list[ (_camera_index + i) % _device_list.size()];
            auto dev = d->create_device(_register_device_notifications);

            if(serial.size() > 0 )
            {
                auto new_serial = dev->get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);

                if(serial == new_serial)
                {
                    res = dev;
                }
            }
            else
            {
                res = dev;
            }
        }

        if (res && cycle_devices)
            _camera_index = ++_camera_index % _device_list.size();

        return res;
    }


    /**
     * If any device is connected return it, otherwise wait until next RealSense device connects.
     * Calling this method multiple times will cycle through connected devices
     */
    std::shared_ptr<device_interface> device_hub::wait_for_device(unsigned int timeout_ms, bool loop_through_devices, const std::string& serial)
    {
        std::unique_lock<std::mutex> lock(_mutex);

        std::shared_ptr<device_interface> res = nullptr;

        // check if there is at least one device connected
        if (_device_list.size() > 0)
        {
            res = create_device(serial, loop_through_devices);
        }

        if (res) return res;

        // block for the requested device to be connected, or till the timeout occurs
        if (!_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]()
        {
            bool cond = false;
            res = nullptr;
            if (_device_list.size() > 0)
            {
                res = create_device(serial, loop_through_devices);
            }
            if (res != nullptr)
                cond = true;
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Avoid busy-wait
            return cond;

        }))
        {
            throw std::runtime_error("No device connected");
        }

        return res;

    }

    /**
    * Checks if device is still connected
    */
    bool device_hub::is_connected(const device_interface& dev)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return dev.is_valid();
    }
}

