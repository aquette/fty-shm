/*  =========================================================================
    fty_shm_cli - fty-shm cli

    Copyright (C) 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    fty_shm_cli - fty-shm cli
@discuss
@end
*/

#include "fty_shm_classes.h"
#include <fty-log/fty_logger.h>
#include <unordered_map>

void print_device(const char *device, const char* filter, bool details){
   std::unordered_map<std::string, std::vector<fty_proto_t*>> list;

    fty::shm::shmMetrics result;
    fty::shm::read_metrics(device, filter, result);

    for (auto &element : result) {
      auto asset = list.find(fty_proto_name(element));
      if(asset != list.end()) {
        asset->second.push_back(element);
      } else {
        list.insert({{fty_proto_name(element),std::vector<fty_proto_t*>()}});
        asset = list.find(fty_proto_name(element));
        asset->second.push_back(element);
      }
    }

   for (auto &dev : list) {
      log_debug ("Device: %s", dev.first.c_str());
      for(auto &metric : dev.second) {
        if(details) {
          fty_proto_print(metric);
        } else {
          char _bufftime[sizeof "YYYY-MM-DDTHH:MM:SSZ"];
          uint64_t _time = fty_proto_time (metric);
          strftime(_bufftime, sizeof _bufftime, "%FT%TZ", gmtime((const time_t*)&_time));
          log_debug ("\t%s(ttl=%" PRIu32"s) %20s@%s = %s%s",
                        _bufftime,
                        fty_proto_ttl (metric),
                        fty_proto_type (metric),
                        fty_proto_name (metric),
                        fty_proto_value (metric),
                        fty_proto_unit (metric));
        }
      }
   }
}


void list_devices() {
   std::unordered_map<std::string, bool> list;
   {
      fty::shm::shmMetrics result;
      fty::shm::read_metrics(".*", ".*", result);

      for (auto &element : result) {
        list.insert({{fty_proto_name(element), true}});

      }
   }
   for (auto &dev : list) {
    log_debug ("%s", dev.first.c_str());
   }
}

int main (int argc, char *argv [])
{
    ManageFtyLog::setInstanceFtylog("fty-metric-cache-cli", "/etc/fty/ftyshmcli.cfg");
    bool verbose = false;
    bool details = false;
    for (int argn = 1; argn < argc; argn++) {
        if (    streq (argv [argn], "--help")
             || streq (argv [argn], "-h"))
        {
            puts ("fty-shm-cli [options]");
            puts ("fty-shm-cli [--details / -d] device [filter] print all information about the device");
            puts ("             device          device name or a regex");
            puts ("             [filter]        regex filter to select specific metric name");
            puts ("             --details / -d  will print full details metrics (fty_proto style) instead of one line style");
            puts ("  --list / -l                print list of devices known to the agent");
            puts ("  --verbose / -v             verbose output");
            puts ("  --help / -h                this information");
            break;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v")) {
            ManageFtyLog::getInstanceFtylog()->setVeboseMode();
        }
        else
        if (    streq (argv [argn], "--list")
             || streq (argv [argn], "-l"))
        {
            list_devices();
            break;
        }
        else {
            if (    streq (argv [argn], "--details")
                  || streq (argv [argn], "-d")) {
              details = true;
              argn++;
            }
            const char* filter=(argn==(argc-2))?argv[argn+1]:".*";
            print_device(argv [argn], filter, details);
            break;
        }
    }
    return 0;
}
