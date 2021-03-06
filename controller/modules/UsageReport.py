﻿# EdgeVPNio
# Copyright 2020, University of Florida
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

import datetime
import hashlib
import threading
try:
    import simplejson as json
except ImportError:
    import json
import urllib.request as urllib2
from framework.ControllerModule import ControllerModule


class UsageReport(ControllerModule):
    def __init__(self, cfx_handle, module_config, module_name):
        super(UsageReport, self).__init__(cfx_handle, module_config, module_name)
        self._stat_data = None 
        self._lck = threading.Lock()
        self._report_id = 0
        self._report = {"Version": self._cfx_handle.query_param("Version"),
                        "NodeId": hashlib.sha256(self.node_id.encode("utf-8")).hexdigest()}
        
    def initialize(self):
        self.log("LOG_INFO", "Module loaded")

    def process_cbt(self, cbt):
        if cbt.op_type == "Request":
            self.req_handler_default(cbt)
        else:
            if cbt.request.action == "TOP_QUERY_KNOWN_PEERS":
                self.resp_handler_query_known_peers(cbt)
            else:
                self.resp_handler_default(cbt)

    def timer_method(self):
        self.register_cbt("Topology", "TOP_QUERY_KNOWN_PEERS", None)

    def terminate(self):
        pass

    def create_report(self, data):
        self._report["ReportId"] = self._report_id
        self._report_id += 1
        for olid in data:
            olid_hash = hashlib.sha256(olid.encode("utf-8")).hexdigest()
            if not olid_hash in self._report:
                self._report[olid_hash] = []
            for peer_id in data[olid]:
                peer_id_hash = hashlib.sha256(peer_id.encode("utf-8")).hexdigest()
                if not peer_id_hash in self._report[olid_hash]:
                    self._report[olid_hash].append(peer_id_hash)

    def submit_report(self, rpt_data):
        self.log("LOG_DEBUG", "report data= %s", rpt_data )
        url = None
        try:
            url = self.config["WebService"]
            req = urllib2.Request(url=url, data=rpt_data)
            req.add_header("Content-Type", "application/json")
            res = urllib2.urlopen(req)
            if res.getcode() != 200:
                self.log("LOG_WARNING", "Usage report server indicated error: %s",
                         res.getcode())
        except (urllib2.HTTPError, urllib2.URLError) as error:
            log = "Usage report submission failed to server {0}. " \
                  "Error: {1}".format(url, error)
            self.log("LOG_WARNING", log)

    def resp_handler_query_known_peers(self, cbt):
        if cbt.response.status:
            data = cbt.response.data
            with self._lck:
                self.create_report(data)
                rpt_data = json.dumps(self._report).encode('utf8')
                self._report = {"Version": self._cfx_handle.query_param("Version"),
                                "NodeId": hashlib.sha256(self.node_id.encode("utf-8")).hexdigest()}
            self.submit_report(rpt_data)
        self.free_cbt(cbt)