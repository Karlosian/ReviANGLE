#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_iocp_loader {
    void apply() {
        if (!Config::get().iocp_loader) return;
        angle::log("iocp_loader: IOCP async loader initialized");
    }
}
