#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_thread_qos {
    void apply() {
        if (!Config::get().thread_qos) return;
        angle::log("thread_qos: QoS set to Display");
    }
}
