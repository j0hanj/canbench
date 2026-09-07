#include "wave/wave.hpp"

#include <string>
#include <vector>

namespace canbench {

namespace {

// left margin so the row labels line up
constexpr int kGutter = 7;

std::string pad_left(std::string s, int w) {
  while (static_cast<int>(s.size()) < w) s.insert(s.begin(), ' ');
  return s;
}

// what to write in the ruler for a run of this field. empty = leave blank
// (the delimiter bits - not worth a label).
std::string ruler_label(Field f) {
  switch (f) {
    case Field::kSof: return "SOF";
    case Field::kId: return "ID";
    case Field::kControl: return "ctl";
    case Field::kDlc: return "DLC";
    case Field::kData: return "DATA";
    case Field::kCrc: return "CRC";
    case Field::kAck: return "ACK";
    case Field::kEof: return "EOF";
    case Field::kIfs: return "IFS";
    case Field::kCrcDelim:
    case Field::kAckDelim: return "";
  }
  return "";
}

}  // namespace

std::string wave(const Frame& f) {
  std::vector<WireBit> wire = annotated_timeline(f);
  const int n = static_cast<int>(wire.size());

  std::string ruler(n, ' ');
  std::string top, bot, stuff(n, ' ');

  // ruler: label each run of one field at its first column
  for (int i = 0; i < n;) {
    Field field = wire[i].field;
    int j = i;
    while (j < n && wire[j].field == field) ++j;
    std::string lab = ruler_label(field);
    if (!lab.empty()) {
      int width = j - i;
      if (static_cast<int>(lab.size()) > width) lab.resize(width);  // "SOF"->"S"
      for (int k = 0; k < static_cast<int>(lab.size()); ++k) ruler[i + k] = lab[k];
      for (int k = static_cast<int>(lab.size()); k < width; ++k) ruler[i + k] = '.';
      if (width >= 4) ruler[j - 1] = ' ';  // little gap so wide fields don't run together
    }
    i = j;
  }

  // the trace: two rows of box chars. idle before SOF is recessive.
  Bit prev = Bit::kRecessive;
  int nstuff = 0;
  for (int i = 0; i < n; ++i) {
    Bit cur = wire[i].level;
    bool rec = (cur == Bit::kRecessive);
    bool up = (prev == Bit::kRecessive);
    if (rec && up) {
      top += "─"; bot += " ";        // ─  stay high
    } else if (rec && !up) {
      top += "┌"; bot += "┘";   // ┌┘ rising edge
    } else if (!rec && !up) {
      top += " "; bot += "─";        // ─  stay low
    } else {
      top += "┐"; bot += "└";   // ┐└ falling edge
    }
    if (wire[i].stuffed) { stuff[i] = '^'; ++nstuff; }
    prev = cur;
  }

  std::string g_field = pad_left("field", kGutter) + "  ";
  std::string g_rec = pad_left("rec", kGutter) + "  ";
  std::string g_dom = pad_left("dom", kGutter) + "  ";
  std::string g_stuff = pad_left("stuff", kGutter) + "  ";

  std::string out;
  out += describe(f);
  out += "   " + std::to_string(n) + " bits on the wire, " +
         std::to_string(nstuff) + " stuffed\n";
  out += g_field + ruler + "\n";
  out += g_rec + top + "\n";
  out += g_dom + bot + "\n";
  out += g_stuff + stuff + "\n";
  out += "  (high = recessive/1, low = dominant/0, ^ = stuff bit)";
  return out;
}

}  // namespace canbench
