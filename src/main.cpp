  /*
   * Copyright (C) 2020  Patrick Pedersen, The TU-DO Makespace

   * This program is free software: you can redistribute it and/or modify
   * it under the terms of the GNU General Public License as published by
   * the Free Software Foundation, either version 3 of the License, or
   * (at your option) any later version.

   * This program is distributed in the hope that it will be useful,
   * but WITHOUT ANY WARRANTY; without even the implied warranty of
   * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   * GNU General Public License for more details.

   * You should have received a copy of the GNU General Public License
   * along with this program.  If not, see <https://www.gnu.org/licenses/>.
   * 
   * Author: Patrick Pedersen <ctx.xda@gmail.com>
   * Description: Main firmware routines for our custom TU-DO Makespace Cafe lights dimmer
   * 
   */

#include <math.h>

#include <Arduino.h>
#include <EEPROM.h>
#include <NeoPixelBus.h> // https://github.com/Makuna/NeoPixelBus

#include "config.h"
#include "LEDStrip.h"
#include "credits.h"
#include "PatchIndicator.h"
#include "PatchEncoder.h"

#ifndef __AVR__
#error Sorry, only AVR boards are currently supported
#endif

#define ANALOG_READ_MAX 1023

#define RGB_HEX_STR_LEN  7 // #AABBCC
#define RGBM_HEX_STR_LEN 9 // #AABBCCDD

//////////////////////////////
// Structs
//////////////////////////////

/* rgbm
 * ----
 * Description: 
 *      A simple struct to store RGB and Main light values.
 *      RGB values are provided as Neopixel RgbColor object
 *      (See https://github.com/Makuna/NeoPixelBus/wiki/RgbColor-object-API).
 */

struct rgbm {
        RgbColor rgb;
        uint8_t M;
};

//////////////////////////////
// Functions
//////////////////////////////

/* uint32_pow
 * ----------
 * Arguments:
 *      base - 8-bit base
 *      pow - 8-bit Power/Exponent
 * Returns:
 *      A 32 bit result of base^pow
 * Description:
 *      Returns a 32 bit result of base^pow
 */

inline uint32_t uint32_pow(uint8_t base, uint8_t pow)
{
        uint32_t ret = 1;

        for (uint8_t i = 0; i < pow; i++)
        ret *= base;

        return ret;
}

/* adc_to_rgb
 * ----------
 * Arguments:
 *      val - A 16 bit analogRead() return
 * Returns:
 *      val >> 2 (= val/4)
 * Description:
 *      Reduces analogRead() values to a byte (0-255)
 */

inline uint8_t adc_to_rgb(uint16_t val)
{
        if (val == 0)
                return 0;
        else if (val < 4)
                return 1;

        return (val >> 2);
}

/* avg_pot_read
 * ----------
 * Arguments:
 *      pin - Pin of Potentiometer
 *      samples - Number of read samples to be averaged
 * Returns:
 *      8-bit average
 * Description:
 *      Reads an 8-bit average value of n potentiometer read samples.
 */

inline uint8_t avg_pot_read(uint8_t pin, uint16_t samples)
{
        uint64_t avg = 0;

        for (uint16_t i = 0; i < samples; i++)

#ifdef POTS_INVERTED
                avg += ANALOG_READ_MAX - analogRead(pin);
#else
                avg += analogRead(pin);
#endif

        return adc_to_rgb(round(avg/samples));
}

/* rgbm_pots_read
 * ----------
 * Arguments:
 *      pot_r - Pin of red potentiometer
 *      pot_g - Pin of green potentiometer
 *      pot_b - Pin of blue potentiometer
 *      pot_m - Pin of main lights potentiometer
 * Returns:
 *      rgbm object of potentiometer values
 * Description:
 *      Returns the current red, green, blue and mains light potentiometer values to an rgbm object
 */

inline rgbm rgbm_pots_read(uint8_t pot_r, uint8_t pot_g, uint8_t pot_b, uint8_t pot_m)
{
        rgbm ret;

#ifdef POTS_INVERTED
        ret.rgb.R = adc_to_rgb(ANALOG_READ_MAX - analogRead(R_POT));
        ret.rgb.G = adc_to_rgb(ANALOG_READ_MAX - analogRead(G_POT));
        ret.rgb.B = adc_to_rgb(ANALOG_READ_MAX - analogRead(B_POT));
        ret.M = adc_to_rgb(ANALOG_READ_MAX - analogRead(M_POT));
#else
        ret.rgb.R = adc_to_rgb(analogRead(R_POT));
        ret.rgb.G = adc_to_rgb(analogRead(G_POT));
        ret.rgb.B = adc_to_rgb(analogRead(B_POT));
        ret.M = adc_to_rgb(analogRead(M_POT));
#endif

#if R_POT_LOWER_BOUND > 0
        if (ret.rgb.R <= R_POT_LOWER_BOUND)
                ret.rgb.R = 0;
#endif

#if G_POT_LOWER_BOUND > 0
        if (ret.rgb.G <= G_POT_LOWER_BOUND)
                ret.rgb.G = 0;
#endif

#if B_POT_LOWER_BOUND > 0
        if (ret.rgb.B <= B_POT_LOWER_BOUND)
                ret.rgb.B = 0;
#endif

#if M_POT_LOWER_BOUND > 0
        if (ret.M <= M_POT_LOWER_BOUND)
                ret.M = 0;
#endif

        return ret;
}

/* avg_rgbm_pot_read
 * ----------
 * Arguments:
 *      pot_r - Pin of red potentiometer
 *      pot_g - Pin of green potentiometer
 *      pot_b - Pin of blue potentiometer
 *      pot_m - Pin of main lights potentiometer
 *      samples - Potentiometer read samples to be averaged
 * Returns:
 *      rgbm object of average potentiometer values
 * Description:
 *      Returns the current red, green, blue and mains light potentiometer values to an rgbm object
 */

inline rgbm avg_rgbm_pot_read(uint8_t pot_r, uint8_t pot_g, uint8_t pot_b, uint8_t pot_m, uint16_t samples)
{
        rgbm ret;

        ret.rgb.R = avg_pot_read(pot_r, samples);
        ret.rgb.G = avg_pot_read(pot_g, samples);
        ret.rgb.B = avg_pot_read(pot_b, samples);
        ret.M = avg_pot_read(pot_m, samples);
        
        return ret;
}

/* rgbm_pot_mov_det
 * ----------
 * Arguments:
 *      rgbmpots - rgbm object with current potentiometer values
 *      avg - rgbm object with average potentiometer values 
 *      max_dev - Max deviation from average (max_dev = |current pots value - avg|)
 * Returns:
 *      True - Potentiometer movement detected (Pot value exceeded the maximum deviation from the average)
 *      False - No significant potentiometer movement detected 
 * Description:
 *      Detects between noise and real potentiometer movement.
 *      Returns true if significant potentiometer movement has been detected.
 */

inline bool rgbm_pot_mov_det(rgbm rgbmpots, rgbm avg, uint8_t max_dev)
{
        return (
                abs(rgbmpots.rgb.R - avg.rgb.R) > max_dev ||
                abs(rgbmpots.rgb.G - avg.rgb.G) > max_dev ||
                abs(rgbmpots.rgb.B - avg.rgb.B) > max_dev
#ifndef NO_MAIN_STRIP
                || abs(rgbmpots.M - avg.M) > max_dev
#endif
        );
}

//////////////////////////////
// Global vars & Objects
//////////////////////////////

// LED Strips
uint8_t mainstrp_bright; // Current brightness of main light strip

#if RGB_STRIP_TYPE == ADDRESSABLE
RGBStrip rgbstrp(RGB_STRIP_LEDS, RGB_STRIP);
#else
RGBStrip rgbstrp(RGB_STRIP_R, RGB_STRIP_G, RGB_STRIP_B);
#endif

rgbm rgbmpots; // Stores current potentiometer values
rgbm avg; // Stores average potentiometer values

rgbm patches[10]; // Patches/Slots of RGBM configurations
uint8_t current_patch; // Currently selected patch

// Rotary Encoder
PatchEncoder patch_encoder(ROTARY_ENC_DT, ROTARY_ENC_CLK, ROTARY_ENC_SW, ROTARY_ENC_DEBOUCE_TIME);

// 7 Segment patch indicator
PatchIndicator patch_indicator (
        SEV_SEG_COMMON_MODE, 
        SEV_SEG_COMMON,
        SEV_SEG_A, 
        SEV_SEG_B, 
        SEV_SEG_C, 
        SEV_SEG_D, 
        SEV_SEG_E, 
        SEV_SEG_F, 
        SEV_SEG_G,
        SEV_SEG_DP
);

// External color programming

// When set to true, the device will maintain its current color
// until potentiometer movement is detected
bool programmed = false;

///////////////////////
// Color via serial
///////////////////////

/* print_rgbm
 * ----------
 * Arguments:
 *      rgbm - rgbm object to be printed
 * Description:
 *      Prints rgbm values to the serial console 
 */

void print_rgbm(rgbm rgbm)
{
        String rgb_hex[4] { String(rgbm.rgb.R, HEX), String(rgbm.rgb.G, HEX), String(rgbm.rgb.B, HEX), String(rgbm.M, HEX)};

        for (uint8_t i = 0; i < 4; i++) {
                if (rgb_hex[i].length() == 1)
                        rgb_hex[i] = "0" + rgb_hex[i];
        }

        Serial.println("Current Color: #" + rgb_hex[0] + rgb_hex[1] + rgb_hex[2] + rgb_hex[3]);
        Serial.println("R: " + String(rgbm.rgb.R));
        Serial.println("G: " + String(rgbm.rgb.G));
        Serial.println("B: " + String(rgbm.rgb.B));
        Serial.println("M: " + String(rgbm.M));
}

/* hexstr_to_uint32
 * ----------
 * Arguments:
 *      *_hex - A return pointer to a uint32_t 
 *      hexstr - A string of hex characters (without 0x prefix)
 * Returns:
 *      True - hex string has successfully been converted to uin32_t
 *      False - Invalid hex character found 
 * Description:
 *      Converts a hex string without prefixes ("0x" etc.) to
 *      a 32 bit value uint32_t. If an invalid hex has been provided,
 *      false is returned.
 */

bool hexstr_to_uint32(String hexstr, uint32_t *_hex)
{
        uint32_t hex = 0;

        for (size_t i = 0; i < hexstr.length(); i++) {
                if (hexstr[i] >= '0' && hexstr[i] <= '9')
                        hex += (hexstr[i] - 0x30) * uint32_pow(16, (hexstr.length() - 1 - i));
                else if (hexstr[i] >= 'A' && hexstr[i] <= 'F')
                        hex += (hexstr[i] - 55) * uint32_pow(16, (hexstr.length() - 1 - i));
                else if (hexstr[i] >= 'a' && hexstr[i] <= 'f')
                        hex += (hexstr[i] - 87) * uint32_pow(16, (hexstr.length() - 1 - i));
                else
                        return false;
        }

        *_hex = hex;
        return true;
}

/* hexstr_to_rgb
 * ----------
 * Arguments:
 *      hex - A rgb hex string (ex. #AABBCC) 
 *      rgb - RgbColor return pointer 
 * Returns:
 *      True - hex string has successfully been parsed to RgbColor object
 *      False - Invalid hex string
 * Description:
 *      Parses a RGB hex string (frequently known as html color codes)
 *      to a NeoPixel RgbColor object. If an invalid hex code is provided,
 *      false is returned.
 */

bool hexstr_to_rgb(String hex, RgbColor *rgb)
{
        uint32_t rgb_hex;
        bool valid;

        if (hex[0] != '#')
                return false;

        valid = hexstr_to_uint32(hex.substring(1, RGB_HEX_STR_LEN), &rgb_hex);
        
        if (valid)
                *rgb = HtmlColor(rgb_hex);
        
        return valid;
}

/* hexstr_to_rgbm
 * ----------
 * Arguments:
 *      hex - A rgbm hex string (ex. #AABBCCDD) 
 *      rgbn - rgbm return pointer 
 * Returns:
 *      True - hex string has successfully been parsed to rgbm object
 *      False - Invalid hex string
 * Description:
 *      Parses a RGBM hex string to a rgbm object. If an invalid hex
 *      code is provided,, false is returned.
 */
bool hexstr_to_rgbm(String hex, rgbm *rgbm)
{
        RgbColor rgb;
        uint32_t m;

        String rgb_hex;
        String m_hex;

        if (hex[0] != '#')
                return false;

        rgb_hex = hex.substring(0, RGB_HEX_STR_LEN);

        if (!hexstr_to_rgb(rgb_hex, &rgb))
                return false;
        
        rgbm->rgb = rgb;
        
        m_hex = hex.substring(RGB_HEX_STR_LEN, RGBM_HEX_STR_LEN);
        
        if (!hexstr_to_uint32(m_hex, &m))
                return false;

        rgbm->M = m;
        return true;
}

/*
 * serialEvent
 * -----------
 * Description:
 *      Processes incoming serial communication:
 *      - When the 'g' command is received, the current color information is emitted
 *      - When a RGB html value (ex. #AABBCC) is received, the RGB strip is programmed to that color
 *      - When a RGBM (RGBA) html value is received (ex. #AABBCCDD), the RGB strip and main light is programmed to that value.
 *        The main light strip brightness is controlled by the last two hex numbers.
 */

void serialEvent()
{
        static String cmdbuf = "";
        while(Serial.available()) {
                char c = (char)Serial.read();

                switch (c) {
                        case 'g': {
                                rgbm rgbm = {
                                        rgbstrp.get(), 
                                        mainstrp_bright
                                };
                                
                                print_rgbm(rgbm);
                                cmdbuf = "";
                                break;
                        }
                        case '\a': {
                                RgbColor prev_color = rgbstrp.get();
                                authors_credit(&rgbstrp);
                                rgbstrp.set(prev_color);
                                cmdbuf = "";
                                break;
                        }
                        case '\n': {
                                bool valid = false;

                                if (cmdbuf.length() == RGB_HEX_STR_LEN) {
                                        RgbColor rgb;
                                        valid = hexstr_to_rgb(cmdbuf, &rgb);

                                        if (valid) {
                                                rgbstrp.set(rgb);
                                        }
                                                
                                } else if (cmdbuf.length() == RGBM_HEX_STR_LEN) {
                                        rgbm rgbm;
                                        valid = hexstr_to_rgbm(cmdbuf, &rgbm);
  
                                        if (valid) {
                                                rgbstrp.set(rgbm.rgb);
#ifndef NO_MAIN_STRIP
                                                mainstrp_bright = rgbm.M;
                                                analogWrite(MAIN_STRIP, mainstrp_bright);
#endif
                                        }
                                }
                                
                                if (valid) {
                                        // Read average of pots for potentiometer movement detection 
                                        avg = avg_rgbm_pot_read(R_POT, G_POT, B_POT, M_POT, POT_MOV_DET_AVG_SAMPLES);
                                        programmed = true;
                                } else {
                                        Serial.println("Invalid hex value!");
                                }

                                cmdbuf = "";
                                break;
                        }
                        default: {
                                cmdbuf += c;
                                break;
                        }
                }
        }
}

//////////////////////////////
// Rotary Encoder Interrupts
//////////////////////////////

/* change_patch
 * ------------
 * Parameters:
 *      up - If set true, the next patch is selected, if 
 *           set false, the previous patch is selected
 * Description:
 *      Changes the patch to the next or previous one
 */

void change_patch(bool up)
{
        bool invalid = false;

        if (up && current_patch < 9)
                current_patch++;
        else if (!up && current_patch > 0)
                current_patch--;
        else
                invalid = true;
  
        if (!invalid) {
                rgbstrp.set(patches[current_patch].rgb);
#ifndef NO_MAIN_STRIP
                mainstrp_bright = patches[current_patch].M;
                analogWrite(MAIN_STRIP, mainstrp_bright);
#endif
                avg = avg_rgbm_pot_read(R_POT, G_POT, B_POT, M_POT, POT_MOV_DET_AVG_SAMPLES);
                programmed = true;
                patch_indicator.set(current_patch);
        }

        patch_indicator.show(PATCH_DISPLAY_TIME);
}

/* patch_up
 * --------
 * Description:
 *      Selects next patch. This function is triggered
 *      by clock-wise rotary encoder movement.
 */

void patch_up()
{
        change_patch(true);
}

/* patch_up
 * --------
 * Description:
 *      Selects previous patch. This function is triggered
 *      by counter-clockwise rotary encoder movement.
 */

void patch_dwn()
{
        change_patch(false);
}

/* save_patch
 * --------
 * Description:
 *      Saves current RGB and main light patch to the patch bank.
 *      This function is triggered by pressing the rotary encoder.
 */

void save_patch()
{
        patches[current_patch].rgb = rgbstrp.get();
        patches[current_patch].M = mainstrp_bright;
        EEPROM.put(EEPROM_PATCH_ADDR + (sizeof(rgbm) * current_patch), patches[current_patch]);
        patch_indicator.blink(NUM_SAVE_BLINKS, BLINK_INTERVAL_ON, BLINK_INTERVAL_OFF);
}


//////////////////////////////
// Initialization
//////////////////////////////

/* setup
 * -----
 * Description:
 *      - Sets the potentiometer pin modes to INPUT
 *      - Sets the main light strip pin mode to OUTPUT
 *      - Prints the boot message (provided in config.h)
 *      - Loads the patches from the EEPROM
 *      - Initializes the RGB light strip from the values of the 0th patch
 *      - Initializes the main light strip from the values of the 0th patch
 *      - Initializes the 7 segment patch indicator
 *      - Initializes the rotary encoder
 */

void setup()
{
#ifdef NO_MAIN_STRIP
        pinMode(M_POT, INPUT_PULLUP);
#else
        pinMode(M_POT, INPUT);
#endif
        pinMode(R_POT, INPUT);
        pinMode(G_POT, INPUT);
        pinMode(B_POT, INPUT);

        pinMode(MAIN_STRIP, OUTPUT);

        Serial.begin(9600);
        Serial.println(String(BOOT_MSG_ASCII_ART) + "\n");
        Serial.println("Author(s): " + String(BOOT_MSG_AUTHORS));
        Serial.println("License: " + String(BOOT_MSG_LICENSE));
        Serial.println("Build date: " + String(__DATE__));
        Serial.println("Documentation: " + String(BOOT_MSG_SRC));

        // Load patches from EEPROM into ram
        EEPROM.get(EEPROM_PATCH_ADDR, patches);

        // Load 0th patch on boot
        current_patch = 0;

        rgbstrp.set(patches[current_patch].rgb);
#ifndef NO_MAIN_STRIP
        mainstrp_bright = patches[current_patch].M;
        analogWrite(MAIN_STRIP, mainstrp_bright); // Sets the brightness of the mainstrip
#endif

        // 7-Segment Initialization
        patch_indicator.set(0);
        patch_indicator.show(PATCH_DISPLAY_TIME);

        avg = avg_rgbm_pot_read(R_POT, G_POT, B_POT, M_POT, POT_MOV_DET_AVG_SAMPLES);
        programmed = true;
}

//////////////////////////////
// Main loop
//////////////////////////////

/*
 * loop
 * -----
 * Description:
 *      The main loop of the dimmer firmware.
 * 
 *       - Read the values of the RGB and main light potentiometers
 *       - Checks if the light has been programmed (ex. by loading a patch or by applying a html code).
 *         If programmed, the RGB and main light are only changed if potentiometer movement is detected.
 *       - RGB light and main lights are set according to the potentiometers
 *       - The rotary encoder is tested
 *       - The patch indicator is updated/handled
 * 
 *       Avoid implementing time intensive instructions/operations, as any delays
 *       will reduce the smoothness of the color transitions.
 */

void loop()
{
        rgbmpots = rgbm_pots_read(R_POT, G_POT, B_POT, M_POT);

        if (!programmed || rgbm_pot_mov_det(rgbmpots, avg, POT_MOV_DET_MAX_DEV)) {
                rgbstrp.set(rgbmpots.rgb); // Set RGB strip
#ifndef NO_MAIN_STRIP
                mainstrp_bright = rgbmpots.M;
                analogWrite(MAIN_STRIP, mainstrp_bright); // Set main light strip
#endif
                programmed = false;
        }

        switch (patch_encoder.action()) {
                case pressed:
                        save_patch();
                        break;
                case left:
                        change_patch(false);
                        break;
                case right:
                        change_patch(true);
                        break;
                default:
                        break;
        }

        if (patch_indicator.busy())
                patch_indicator.update();
}