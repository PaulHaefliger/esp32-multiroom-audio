#pragma once

/* Joins the satellite's network and blocks until an IP is assigned.
   Reconnects automatically. */
void wifi_sta_start(void);
