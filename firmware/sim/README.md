# UI simulator

The panel UI in `firmware/src/ui` is LVGL 9 and is compiled by the `kmj` environment against the LovyanGFX flush in `src/port/lvgl_port.cpp`.

A desktop SDL build of the same screens needs LVGL's SDL driver (`LV_USE_SDL`) and a `BoardLink` that feeds pointer events instead of the CST226. That driver is not the firmware default: the flush callback, touch read, and DMA buffers only exist on the ESP32-S3. Iterate on the device with `pio run -e kmj -t upload`, or run the host logic tests with `pio test -e native`.
