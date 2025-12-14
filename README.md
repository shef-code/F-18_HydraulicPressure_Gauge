# F-18 Hydraulic Pressure Gauge

Hydraulic Pressure gauge in the Right AUX panel of the F-18. 

This code is primarily based on source Tanarg posted in the DCS Forums.
It has been modified to work with the Waveshare ESP32-S3-LCD-1.28.
The original code was modified to prevent the continious create/destroy of the sprites. DCS callbacks no longer trigger a re-write of the sprite directly.


<img width="281" height="280" alt="ESP32-S3-Touch-LCD-1 28-usenotes-03" src="https://github.com/user-attachments/assets/f2d18af4-637b-487d-b24f-f8862d516707" />

Additional changes are required for the display to render.
Included: 
TFT_eSPI support files
Arduino Sketch with updates to work with DCS World and the ESP32S3 controller used by Waveshare.

Aruduino IDE Settings should be configured as such:
<br>
<img width="552" height="980" alt="image" src="https://github.com/user-attachments/assets/3482b5d7-2889-4fbc-9fee-45bccfe5c5c5" />

<B> TFT_eSPI Library Modifications </B>
<p>

Replace the user_setup_select.h file in the TFT_eSPI folder<br>
Add Custom_Waveshare_ESP32S31_28LCD.h tp the User_Setups folder.
 
</p>

