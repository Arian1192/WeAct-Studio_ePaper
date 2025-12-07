#include "weact_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace weact_epaper {

// Implementation notes for WeAct 1.54" E-Paper Display
// - Resolution: 200x200 pixels
// - Type: Black & White (monochrome)
// - Controller: Similar to WeAct 2.9" and 4.2" models
// - Reference: https://github.com/WeActStudio/WeActStudio.EpaperModule
// - Datasheet: ZJY120M10 / SSD1681

static const char *const TAG = "weact_1.54";

static const uint16_t HEIGHT = 200;
static const uint16_t WIDTH = 200;

// General Commands
static const uint8_t SW_RESET = 0x12;
static const uint8_t ACTIVATE = 0x20;
static const uint8_t WRITE_BLACK = 0x24;
static const uint8_t SLEEP[] = {0x10, 0x01};
static const uint8_t UPDATE_FULL[] = {0x22, 0xF7};

// Configuration commands
static const uint8_t DRV_OUT_CTL[] = {0x01, 0xC7, 0x00, 0x00};  // Driver output control
static const uint8_t DATA_ENTRY[] = {0x11, 0x03};               // Data entry mode
static const uint8_t BORDER_FULL[] = {0x3C, 0x00};              // Border waveform control
static const uint8_t TEMP_SENS[] = {0x18, 0x80};                // Use internal temp sensor
static const uint8_t DISPLAY_UPDATE[] = {0x21, 0x00, 0x80};     // Display update control

// For controlling which part of the image we want to write
static const uint8_t RAM_X_RANGE[] = {0x44, 0x00, ((WIDTH / 8u) - 1)};
static const uint8_t RAM_Y_RANGE[] = {0x45, 0x00, 0x00, (HEIGHT - 1), (HEIGHT >> 8)};
static const uint8_t RAM_X_POS[] = {0x4E, 0x00};  // Always start at 0
static const uint8_t RAM_Y_POS = 0x4F;

// Helper macro for sending commands with data
#define SEND(x) this->cmd_data(x, sizeof(x))

// Basics

int WeActEPaper1P54In::get_width_internal() { return WIDTH; }
int WeActEPaper1P54In::get_height_internal() { return HEIGHT; }
uint32_t WeActEPaper1P54In::idle_timeout_() { return 2500; }

void WeActEPaper1P54In::dump_config() {
  LOG_DISPLAY("", "WeAct E-Paper", this)
  ESP_LOGCONFIG(TAG, "  Model: 1.54in");
  LOG_PIN("  CS Pin: ", this->cs_)
  LOG_PIN("  Reset Pin: ", this->reset_pin_)
  LOG_PIN("  DC Pin: ", this->dc_pin_)
  LOG_PIN("  Busy Pin: ", this->busy_pin_)
  LOG_UPDATE_INTERVAL(this);
}

// Device lifecycle

void WeActEPaper1P54In::setup() {
  setup_pins_();
  delay(20);
  this->send_reset_();
  delay(100);  // NOLINT
  this->wait_until_idle_();
  this->command(SW_RESET);
  this->wait_until_idle_();

  SEND(DRV_OUT_CTL);
  SEND(DATA_ENTRY);
  SEND(BORDER_FULL);
  SEND(TEMP_SENS);
  SEND(DISPLAY_UPDATE);

  this->wait_until_idle_();
}

void WeActEPaper1P54In::send_reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(2);
    this->reset_pin_->digital_write(true);
  }
}

// Must implement, but we override setup to have more control
void WeActEPaper1P54In::initialize() {}

void WeActEPaper1P54In::deep_sleep() { SEND(SLEEP); }

// Pixel stuff

// t and b are y positions, i.e. line numbers.
void WeActEPaper1P54In::set_window_(int t, int b) {
  SEND(RAM_X_RANGE);
  SEND(RAM_Y_RANGE);
  SEND(RAM_X_POS);

  uint8_t buffer[3];
  buffer[0] = RAM_Y_POS;
  buffer[1] = (uint8_t) t % 256;
  buffer[2] = (uint8_t) (t / 256);
  SEND(buffer);
}

// Send the buffer starting on line `top`, up to line `bottom`.
void WeActEPaper1P54In::write_buffer_(int top, int bottom) {
  auto width_bytes = this->get_width_internal() / 8u;
  auto offset = top * width_bytes;
  auto length = (bottom - top) * width_bytes;

  this->wait_until_idle_();
  this->set_window_(top, bottom);

  this->command(WRITE_BLACK);
  this->start_data_();
  this->write_array(this->buffer_ + offset, length);
  this->end_data_();
}

void HOT WeActEPaper1P54In::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0)
    return;

  const uint32_t pos = (x + y * this->get_width_internal()) / 8u;
  const uint8_t subpos = 0x80 >> (x & 0x07);

  if (color == display::COLOR_OFF) {
    this->buffer_[pos] |= subpos;
  } else {
    this->buffer_[pos] &= ~subpos;
  }
}

void WeActEPaper1P54In::full_update_() {
  ESP_LOGI(TAG, "Performing full e-paper update.");
  this->write_buffer_(0, this->get_height_internal());
  SEND(UPDATE_FULL);
  this->command(ACTIVATE);
  this->is_busy_ = false;
}

void WeActEPaper1P54In::display() {
  if (this->is_busy_ || (this->busy_pin_ != nullptr && this->busy_pin_->digital_read()))
    return;
  this->is_busy_ = true;
  this->full_update_();
}

}  // namespace weact_epaper
}  // namespace esphome
