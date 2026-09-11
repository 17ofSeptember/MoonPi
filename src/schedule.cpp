#include "moonpi/schedule.hpp"
#include <charconv>
#include <sstream>
namespace moonpi {
namespace {
int integer(std::string_view s) {
  int value = 0;
  const auto parsed = std::from_chars(s.data(), s.data() + s.size(), value);
  if (s.empty() || parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
    throw Error("schedule.syntax", "Invalid numeric schedule field");
  return value;
}
std::bitset<64> field(const std::string &text, int low, int high) {
  std::bitset<64> result;
  size_t start = 0;
  do {
    const auto comma = text.find(',', start);
    const auto part = text.substr(start, comma == std::string::npos ? comma : comma - start);
    const auto slash = part.find('/');
    const auto range = part.substr(0, slash);
    const int step = slash == std::string::npos ? 1 : integer(part.substr(slash + 1));
    int first = low, last = high;
    if (range != "*") {
      const auto dash = range.find('-');
      first = integer(range.substr(0, dash));
      last = dash == std::string::npos ? first : integer(range.substr(dash + 1));
    }
    if (first < low || last > high || first > last || step < 1 || step > high - low + 1)
      throw Error("schedule.range", "Schedule field is out of range");
    for (int n = first; n <= last; n += step)
      result.set(static_cast<size_t>(n));
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  } while (start <= text.size());
  return result;
}
} // namespace
Cron::Cron(const std::string &expression) {
  if (expression.size() > 128)
    throw Error("schedule.size", "Cron expression exceeds 128 characters");
  std::istringstream input(expression);
  std::array<std::string, 5> pieces;
  std::string extra;
  for (auto &p : pieces)
    if (!(input >> p))
      throw Error("schedule.syntax",
                  "Cron needs five numeric fields: minute hour day month weekday");
  if (input >> extra)
    throw Error("schedule.syntax", "Cron needs exactly five fields");
  const int low[] = {0, 0, 1, 1, 0}, high[] = {59, 23, 31, 12, 6};
  for (size_t i = 0; i < 5; ++i)
    fields_[i] = field(pieces[i], low[i], high[i]);
  any_day_ = pieces[2] == "*";
  any_weekday_ = pieces[4] == "*";
}
bool Cron::matches(int64_t seconds) const {
  using namespace std::chrono;
  const sys_seconds time{std::chrono::seconds{seconds}};
  const auto date = floor<days>(time);
  const year_month_day ymd{date};
  const hh_mm_ss clock{time - date};
  const bool dom = fields_[2].test(static_cast<unsigned>(ymd.day())),
             dow = fields_[4].test(weekday{date}.c_encoding());
  const bool day = any_day_ ? dow : any_weekday_ ? dom : dom || dow;
  return fields_[0].test(static_cast<size_t>(clock.minutes().count())) &&
         fields_[1].test(static_cast<size_t>(clock.hours().count())) &&
         fields_[3].test(static_cast<unsigned>(ymd.month())) && day;
}
int64_t parse_utc(const std::string &text) {
  if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
      text[16] != ':' || text[19] != 'Z')
    throw Error("schedule.timestamp", "Use UTC timestamp YYYY-MM-DDTHH:MM:SSZ");
  const int year = integer(text.substr(0, 4)), month = integer(text.substr(5, 2)),
            day = integer(text.substr(8, 2)), hour = integer(text.substr(11, 2)),
            minute = integer(text.substr(14, 2)), second = integer(text.substr(17, 2));
  const std::chrono::year_month_day date{std::chrono::year{year},
                                         std::chrono::month{static_cast<unsigned>(month)},
                                         std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || year < 1970 || year > 2200 || hour > 23 || minute > 59 || second > 59 ||
      hour < 0 || minute < 0 || second < 0)
    throw Error("schedule.timestamp", "Invalid UTC date/time");
  return std::chrono::duration_cast<std::chrono::seconds>(
             (std::chrono::sys_days{date} + std::chrono::hours(hour) +
              std::chrono::minutes(minute) + std::chrono::seconds(second))
                 .time_since_epoch())
      .count();
}
CalendarScheduler::CalendarScheduler(fs::path path) : path_(std::move(path)) {
  if (path_.empty())
    return;
  try {
    if (!fs::exists(path_))
      return;
    const auto state = read_json(path_);
    if (state.at("version") != 1 || !state.at("records").is_object() ||
        state.at("records").size() > 1024)
      throw Error("schedule.state", "Invalid scheduler state");
    for (const auto &[id, r] : state.at("records").items())
      if (id.size() > 256 || !r.at("fingerprint").is_string() ||
          r.at("fingerprint").get<std::string>().size() > 2048 ||
          !r.at("slot").is_number_integer() || r.at("slot") < 0 || r.at("slot") > 7289654399LL)
        throw Error("schedule.state", "Invalid scheduler checkpoint");
    records_ = state.at("records");
  } catch (const std::exception &e) {
    load_error_ = e.what();
  }
}
void CalendarScheduler::configure(const std::vector<CompiledNode> &nodes, int64_t now) {
  entries_.clear();
  start_ = now;
  last_minute_ = now / 60;
  for (const auto &node : nodes) {
    if (node.driver != "moonpi.driver.cron" && node.driver != "moonpi.driver.once")
      continue;
    if (!load_error_.empty())
      throw Error("schedule.protected", "Scheduler state is corrupt and protected: " + load_error_);
    Entry e;
    e.node = node.id;
    e.fingerprint = node.driver + node.properties.dump();
    if (node.driver == "moonpi.driver.cron")
      e.cron.emplace(node.properties.at("expression"));
    else {
      e.at = parse_utc(node.properties.at("at_utc"));
      e.catch_up = node.properties.at("missed") == "RUN_ONCE_AFTER_START";
    }
    entries_.push_back(std::move(e));
  }
}
std::vector<std::string> CalendarScheduler::due(int64_t now) {
  if (entries_.empty() || (now / 60 <= last_minute_ &&
                           std::none_of(entries_.begin(), entries_.end(), [&](const auto &e) {
                             return !e.cron && !e.checked && now >= e.at;
                           })))
    return {};
  std::vector<std::string> result;
  auto next = records_;
  bool changed = false;
  for (auto &entry : entries_) {
    int64_t slot = 0;
    bool emit = false;
    if (entry.cron) {
      if (now / 60 <= last_minute_ || !entry.cron->matches(now))
        continue;
      slot = now / 60;
      emit = true;
    } else {
      if (entry.checked || now < entry.at)
        continue;
      slot = entry.at;
      emit = entry.at >= start_ || entry.catch_up;
    }
    if (next.contains(entry.node) && next[entry.node]["fingerprint"] == entry.fingerprint &&
        next[entry.node]["slot"].get<int64_t>() >= slot)
      continue;
    if (!next.contains(entry.node) && next.size() >= 1024)
      throw Error("schedule.limit", "Scheduler checkpoint limit reached");
    next[entry.node] = {{"fingerprint", entry.fingerprint}, {"slot", slot}};
    changed = true;
    if (emit)
      result.push_back(entry.node);
  }
  // Persist before emitting: at-most-once across crashes, never replay actuators after restart.
  if (changed && !path_.empty())
    atomic_write(path_, Json{{"version", 1}, {"records", next}}.dump(2));
  records_ = std::move(next);
  last_minute_ = std::max(last_minute_, now / 60);
  for (auto &entry : entries_)
    if (!entry.cron && now >= entry.at)
      entry.checked = true;
  std::erase_if(entries_, [](const auto &e) { return !e.cron && e.checked; });
  return result;
}
} // namespace moonpi
