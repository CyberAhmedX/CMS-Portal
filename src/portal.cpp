#include "cms/portal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

#include "cms/storage.h"

namespace cms {

namespace {

constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool sameName(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

std::optional<Role> parseRole(const std::string& text) {
    if (text == "admin") return Role::Admin;
    if (text == "faculty") return Role::Faculty;
    if (text == "student") return Role::Student;
    return std::nullopt;
}

std::optional<std::int64_t> parseLong(const std::string& text) {
    if (text.empty() || text.size() > 18) return std::nullopt;
    std::size_t start = text[0] == '-' ? 1 : 0;
    if (start == text.size()) return std::nullopt;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    }
    return std::stoll(text);
}

std::optional<int> parseInt(const std::string& text) {
    std::optional<std::int64_t> value = parseLong(text);
    if (!value || *value < -2147483647LL || *value > 2147483647LL) return std::nullopt;
    return static_cast<int>(*value);
}

}  // namespace

int attendancePercent(int attended, int total) {
    return total <= 0 ? 0 : (attended * 100 + total / 2) / total;
}

std::string roleName(Role role) {
    switch (role) {
        case Role::Admin: return "admin";
        case Role::Faculty: return "faculty";
        case Role::Student: return "student";
    }
    return "unknown";
}

// ---------------------------------------------------------------- passwords

PasswordHash PasswordHash::create(const std::string& password, std::uint32_t iterations) {
    PasswordHash result;
    result.iterations = iterations;
    result.salt = crypto::randomBytes(kSaltBytes);
    result.hash = crypto::pbkdf2Sha256(password, result.salt, iterations, kHashBytes);
    return result;
}

bool PasswordHash::matches(const std::string& password) const {
    if (iterations == 0 || hash.empty()) return false;
    return crypto::constantTimeEqual(crypto::pbkdf2Sha256(password, salt, iterations, hash.size()), hash);
}

std::string PasswordHash::encode() const {
    return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + crypto::toHex(salt) + "$" +
           crypto::toHex(hash);
}

std::optional<PasswordHash> PasswordHash::decode(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t pos; (pos = text.find('$', start)) != std::string::npos; start = pos + 1) {
        parts.push_back(text.substr(start, pos - start));
    }
    parts.push_back(text.substr(start));
    if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") return std::nullopt;

    std::optional<int> iterations = parseInt(parts[1]);
    PasswordHash result;
    result.salt = crypto::fromHex(parts[2]);
    result.hash = crypto::fromHex(parts[3]);
    if (!iterations || *iterations < 1 || result.salt.empty() || result.hash.empty()) return std::nullopt;
    result.iterations = static_cast<std::uint32_t>(*iterations);
    return result;
}

User::User(std::string username, PasswordHash password)
    : username_(std::move(username)), password_(std::move(password)) {}

std::unique_ptr<User> makeUser(Role role, std::string username, PasswordHash password) {
    switch (role) {
        case Role::Admin: return std::make_unique<Admin>(std::move(username), std::move(password));
        case Role::Faculty: return std::make_unique<Faculty>(std::move(username), std::move(password));
        case Role::Student: return std::make_unique<Student>(std::move(username), std::move(password));
    }
    return nullptr;
}

// ---------------------------------------------------------------- input rules

std::string Portal::normalizeUsername(const std::string& username) { return lower(username); }

void Portal::checkUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        throw PortalError("Username must be 3 to 32 characters.");
    }
    for (char c : username) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        if (!ok) throw PortalError("Username can only use letters, digits, '.', '_' and '-'.");
    }
}

void Portal::checkPassword(const std::string& username, const std::string& password) {
    if (password.size() < static_cast<std::size_t>(kMinPassword)) {
        throw PortalError("Password must be at least " + std::to_string(kMinPassword) + " characters.");
    }
    if (password.size() > static_cast<std::size_t>(kMaxPassword)) {
        throw PortalError("Password must be at most " + std::to_string(kMaxPassword) + " characters.");
    }
    if (sameName(password, username)) throw PortalError("Password can't be the same as the username.");
}

std::string Portal::cleanSubjectName(const std::string& name) {
    std::string out;
    bool space = false;
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u)) {
            space = !out.empty();
            continue;
        }
        if (!std::isprint(u)) throw PortalError("Subject name has an unsupported character.");
        if (space) out.push_back(' ');
        out.push_back(c);
        space = false;
    }
    if (out.size() < 2 || out.size() > 40) throw PortalError("Subject name must be 2 to 40 characters.");
    return out;
}

bool Portal::isValidDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (!std::isdigit(static_cast<unsigned char>(date[i]))) return false;
    }
    int year = std::stoi(date.substr(0, 4));
    int month = std::stoi(date.substr(5, 2));
    int day = std::stoi(date.substr(8, 2));
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int limit = days[month - 1] + (month == 2 && leap ? 1 : 0);
    return day <= limit;
}

// ---------------------------------------------------------------- portal

Portal::Portal(Options options) : options_(std::move(options)) {
    if (!options_.clock) {
        options_.clock = [] {
            return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count());
        };
    }
    load();
}

std::int64_t Portal::now() const { return options_.clock(); }

std::string Portal::today() const {
    std::time_t t = static_cast<std::time_t>(now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[11];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &local);
    return buffer;
}

std::string Portal::term() const {
    std::string date = today();
    int month = std::stoi(date.substr(5, 2));
    const char* season = month <= 5 ? "Spring" : month <= 7 ? "Summer" : "Fall";
    return std::string(season) + " " + date.substr(0, 4);
}

bool Portal::needsSetup() const {
    return std::none_of(users_.begin(), users_.end(),
                        [](const auto& u) { return u->role() == Role::Admin; });
}

void Portal::createFirstAdmin(const std::string& username, const std::string& password) {
    if (!needsSetup()) throw PortalError("An admin account already exists.");
    std::string name = normalizeUsername(username);
    checkUsername(name);
    checkPassword(name, password);
    if (findMutable(name)) throw PortalError("That username is taken.");
    users_.push_back(makeUser(Role::Admin, name, PasswordHash::create(password, options_.iterations)));
    saveUsers();
}

void Portal::registerUser(Role role, const std::string& username, const std::string& password) {
    if (role == Role::Admin) throw PortalError("Admin accounts can't be self-registered.");
    std::string name = normalizeUsername(username);
    checkUsername(name);
    checkPassword(name, password);
    // Usernames are unique across every role, so nobody can register a
    // second account with someone else's name and take over their records.
    if (findMutable(name)) throw PortalError("That username is taken.");
    users_.push_back(makeUser(role, name, PasswordHash::create(password, options_.iterations)));
    saveUsers();
}

LoginResult Portal::login(Role role, const std::string& username, const std::string& password) {
    User* user = findMutable(normalizeUsername(username));
    if (!user || user->role() != role) {
        // Hash anyway, so an unknown username takes as long as a wrong
        // password and the response time doesn't reveal which accounts exist.
        if (!dummy_) dummy_ = PasswordHash::create("not-a-real-password", options_.iterations);
        dummy_->matches(password);
        return {LoginStatus::WrongCredentials, nullptr, 0};
    }

    std::int64_t current = now();
    if (user->lockedUntil > current) return {LoginStatus::Locked, nullptr, user->lockedUntil - current};

    if (!user->password().matches(password)) {
        if (++user->failedLogins >= options_.maxFailedLogins) {
            user->failedLogins = 0;
            user->lockedUntil = current + options_.lockSeconds;
            saveUsers();
            return {LoginStatus::Locked, nullptr, options_.lockSeconds};
        }
        saveUsers();
        return {LoginStatus::WrongCredentials, nullptr, 0};
    }

    if (user->failedLogins || user->lockedUntil) {
        user->failedLogins = 0;
        user->lockedUntil = 0;
        saveUsers();
    }
    return {LoginStatus::Ok, user, 0};
}

void Portal::changePassword(const User& user, const std::string& current, const std::string& next) {
    User* target = findMutable(user.username());
    if (!target) throw PortalError("Account not found.");
    if (!target->password().matches(current)) throw PortalError("Current password is wrong.");
    checkPassword(target->username(), next);
    target->setPassword(PasswordHash::create(next, options_.iterations));
    saveUsers();
}

User* Portal::findMutable(const std::string& username) {
    for (auto& u : users_) {
        if (u->username() == username) return u.get();
    }
    return nullptr;
}

const User* Portal::findUser(Role role, const std::string& username) const {
    std::string name = normalizeUsername(username);
    for (const auto& u : users_) {
        if (u->role() == role && u->username() == name) return u.get();
    }
    return nullptr;
}

std::vector<const User*> Portal::users(Role role) const {
    std::vector<const User*> out;
    for (const auto& u : users_) {
        if (u->role() == role) out.push_back(u.get());
    }
    std::sort(out.begin(), out.end(),
              [](const User* a, const User* b) { return a->username() < b->username(); });
    return out;
}

void Portal::requireRole(const User& user, Role role) {
    if (user.role() != role) throw PortalError("Only " + roleName(role) + " accounts can do that.");
}

void Portal::resetPassword(const User& admin, const std::string& username, const std::string& password) {
    requireRole(admin, Role::Admin);
    User* target = findMutable(normalizeUsername(username));
    if (!target) throw PortalError("No account with that username.");
    checkPassword(target->username(), password);
    target->setPassword(PasswordHash::create(password, options_.iterations));
    target->failedLogins = 0;
    target->lockedUntil = 0;
    saveUsers();
}

// ---------------------------------------------------------------- subjects

void Portal::addSubject(const User& faculty, const std::string& name, int creditHours) {
    requireRole(faculty, Role::Faculty);
    std::string clean = cleanSubjectName(name);
    if (creditHours < kMinCredits || creditHours > kMaxCredits) {
        throw PortalError("Credit hours must be " + std::to_string(kMinCredits) + " to " +
                          std::to_string(kMaxCredits) + ".");
    }
    if (findSubject(clean)) throw PortalError("A subject with that name already exists.");
    subjects_.push_back({clean, creditHours, faculty.username()});
    saveSubjects();
}

std::vector<Subject> Portal::subjects() const {
    std::vector<Subject> out = subjects_;
    std::sort(out.begin(), out.end(),
              [](const Subject& a, const Subject& b) { return lower(a.name) < lower(b.name); });
    return out;
}

std::vector<Subject> Portal::subjectsTaughtBy(const User& faculty) const {
    std::vector<Subject> out;
    for (const Subject& s : subjects()) {
        if (s.faculty == faculty.username()) out.push_back(s);
    }
    return out;
}

std::optional<Subject> Portal::findSubject(const std::string& name) const {
    for (const Subject& s : subjects_) {
        if (sameName(s.name, name)) return s;
    }
    return std::nullopt;
}

const Subject& Portal::requireOwnSubject(const User& faculty, const std::string& subject) const {
    requireRole(faculty, Role::Faculty);
    for (const Subject& s : subjects_) {
        if (!sameName(s.name, subject)) continue;
        if (s.faculty != faculty.username()) throw PortalError("You don't teach " + s.name + ".");
        return s;
    }
    throw PortalError("No subject with that name.");
}

// ---------------------------------------------------------------- enrollment

void Portal::enroll(const User& student, const std::string& subject) {
    requireRole(student, Role::Student);
    std::optional<Subject> s = findSubject(subject);
    if (!s) throw PortalError("No subject with that name.");
    for (const auto& [who, what] : enrollments_) {
        if (who == student.username() && what == s->name) {
            throw PortalError("You are already registered for " + s->name + ".");
        }
    }
    enrollments_.emplace_back(student.username(), s->name);
    saveEnrollments();
}

std::vector<std::string> Portal::enrolledSubjects(const std::string& student) const {
    std::vector<std::string> out;
    for (const auto& [who, what] : enrollments_) {
        if (who == student) out.push_back(what);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return lower(a) < lower(b); });
    return out;
}

std::vector<std::string> Portal::studentsIn(const std::string& subject) const {
    std::vector<std::string> out;
    for (const auto& [who, what] : enrollments_) {
        if (sameName(what, subject)) out.push_back(who);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------- attendance

bool Portal::hasAttendance(const std::string& subject, const std::string& date) const {
    return std::any_of(attendance_.begin(), attendance_.end(), [&](const AttendanceRecord& r) {
        return r.date == date && sameName(r.subject, subject);
    });
}

void Portal::recordAttendance(const User& faculty, const std::string& subject, const std::string& date,
                              const std::map<std::string, bool>& present) {
    const Subject& s = requireOwnSubject(faculty, subject);
    if (!isValidDate(date)) throw PortalError("Date must be a real date written as YYYY-MM-DD.");
    if (date > today()) throw PortalError("Attendance can't be marked for a future date.");

    std::vector<std::string> enrolled = studentsIn(s.name);
    if (enrolled.empty()) throw PortalError("No students are registered for " + s.name + " yet.");
    for (const auto& [student, _] : present) {
        if (std::find(enrolled.begin(), enrolled.end(), student) == enrolled.end()) {
            throw PortalError(student + " is not registered for " + s.name + ".");
        }
    }
    for (const std::string& student : enrolled) {
        if (!present.count(student)) throw PortalError("Attendance is missing for " + student + ".");
    }

    // Replace only this one session. Every other date and subject is kept.
    attendance_.erase(std::remove_if(attendance_.begin(), attendance_.end(),
                                     [&](const AttendanceRecord& r) {
                                         return r.date == date && r.subject == s.name;
                                     }),
                      attendance_.end());
    for (const auto& [student, here] : present) attendance_.push_back({date, s.name, student, here});
    saveAttendance();
}

std::vector<AttendanceSummary> Portal::attendanceFor(const std::string& student) const {
    std::vector<AttendanceSummary> out;
    for (const std::string& subject : enrolledSubjects(student)) {
        AttendanceSummary summary{subject, 0, 0};
        for (const AttendanceRecord& r : attendance_) {
            if (r.student != student || r.subject != subject) continue;
            ++summary.total;
            if (r.present) ++summary.attended;
        }
        out.push_back(summary);
    }
    return out;
}

std::vector<AttendanceRecord> Portal::attendanceRecords(const std::string& subject) const {
    std::vector<AttendanceRecord> out;
    for (const AttendanceRecord& r : attendance_) {
        if (sameName(r.subject, subject)) out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.date != b.date ? a.date < b.date : a.student < b.student;
    });
    return out;
}

void Portal::setMinAttendance(const User& admin, int percent) {
    requireRole(admin, Role::Admin);
    if (percent < kLowestMinAttendance || percent > kHighestMinAttendance) {
        throw PortalError("The attendance requirement must be " + std::to_string(kLowestMinAttendance) + "% to " +
                          std::to_string(kHighestMinAttendance) + "%.");
    }
    minAttendance_ = percent;
    saveSettings();
}

bool Portal::isEligible(const std::string& student, const std::string& subject) const {
    int attended = 0, total = 0;
    for (const AttendanceRecord& r : attendance_) {
        if (r.student != student || !sameName(r.subject, subject)) continue;
        ++total;
        if (r.present) ++attended;
    }
    return total == 0 || attendancePercent(attended, total) >= minAttendance_;
}

std::vector<AttendanceShortage> Portal::shortages() const {
    std::vector<AttendanceShortage> out;
    for (const auto& [student, subject] : enrollments_) {
        int attended = 0, total = 0;
        for (const AttendanceRecord& r : attendance_) {
            if (r.student != student || r.subject != subject) continue;
            ++total;
            if (r.present) ++attended;
        }
        if (total && attendancePercent(attended, total) < minAttendance_) out.push_back({student, subject, attended, total});
    }
    std::sort(out.begin(), out.end(), [](const AttendanceShortage& a, const AttendanceShortage& b) {
        int pa = attendancePercent(a.attended, a.total), pb = attendancePercent(b.attended, b.total);
        if (pa != pb) return pa < pb;
        return a.student != b.student ? a.student < b.student : a.subject < b.subject;
    });
    return out;
}

std::vector<AttendanceRecord> Portal::attendanceHistory(const std::string& student, const std::string& subject) const {
    std::vector<AttendanceRecord> out;
    for (const AttendanceRecord& r : attendanceRecords(subject)) {
        if (r.student == student) out.push_back(r);
    }
    return out;  // attendanceRecords() is already sorted by date
}

// ---------------------------------------------------------------- marks

void Portal::setMark(const User& faculty, const std::string& subject, const std::string& student, int mark) {
    const Subject& s = requireOwnSubject(faculty, subject);
    std::string name = normalizeUsername(student);
    std::vector<std::string> enrolled = studentsIn(s.name);
    if (std::find(enrolled.begin(), enrolled.end(), name) == enrolled.end()) {
        throw PortalError(name + " is not registered for " + s.name + ".");
    }
    if (mark < kMinMark || mark > kMaxMark) {
        throw PortalError("Mark must be " + std::to_string(kMinMark) + " to " + std::to_string(kMaxMark) + ".");
    }
    for (Mark& m : marks_) {
        if (m.student == name && m.subject == s.name) {
            m.mark = mark;
            saveMarks();
            return;
        }
    }
    marks_.push_back({name, s.name, mark});
    saveMarks();
}

std::vector<Mark> Portal::marksFor(const std::string& student) const {
    std::vector<Mark> out;
    for (const Mark& m : marks_) {
        if (m.student == student) out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Mark& a, const Mark& b) { return lower(a.subject) < lower(b.subject); });
    return out;
}

std::vector<Mark> Portal::marksIn(const std::string& subject) const {
    std::vector<Mark> out;
    for (const Mark& m : marks_) {
        if (sameName(m.subject, subject)) out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Mark& a, const Mark& b) { return a.student < b.student; });
    return out;
}

// ---------------------------------------------------------------- files

void Portal::load() {
    namespace fs = std::filesystem;
    const fs::path& dir = options_.dataDir;

    for (const auto& row : storage::readTable(dir / "users.tsv")) {
        if (row.size() != 5) continue;
        std::optional<Role> role = parseRole(row[0]);
        std::optional<PasswordHash> hash = PasswordHash::decode(row[2]);
        if (!role || !hash || findMutable(row[1])) continue;
        auto user = makeUser(*role, row[1], *hash);
        user->failedLogins = parseInt(row[3]).value_or(0);
        user->lockedUntil = parseLong(row[4]).value_or(0);
        users_.push_back(std::move(user));
    }
    for (const auto& row : storage::readTable(dir / "subjects.tsv")) {
        std::optional<int> credits = row.size() == 3 ? parseInt(row[2]) : std::nullopt;
        if (credits) subjects_.push_back({row[1], *credits, row[0]});
    }
    for (const auto& row : storage::readTable(dir / "enrollments.tsv")) {
        if (row.size() == 2) enrollments_.emplace_back(row[0], row[1]);
    }
    for (const auto& row : storage::readTable(dir / "attendance.tsv")) {
        if (row.size() == 4 && (row[3] == "P" || row[3] == "A")) {
            attendance_.push_back({row[0], row[1], row[2], row[3] == "P"});
        }
    }
    for (const auto& row : storage::readTable(dir / "marks.tsv")) {
        std::optional<int> mark = row.size() == 3 ? parseInt(row[2]) : std::nullopt;
        if (mark) marks_.push_back({row[0], row[1], *mark});
    }
    for (const auto& row : storage::readTable(dir / "settings.tsv")) {
        if (row.size() != 2 || row[0] != "min_attendance") continue;
        std::optional<int> value = parseInt(row[1]);
        if (value && *value >= kLowestMinAttendance && *value <= kHighestMinAttendance) minAttendance_ = *value;
    }
}

void Portal::saveUsers() const {
    std::vector<storage::Row> rows;
    for (const auto& u : users_) {
        rows.push_back({roleName(u->role()), u->username(), u->password().encode(),
                        std::to_string(u->failedLogins), std::to_string(u->lockedUntil)});
    }
    storage::writeTable(options_.dataDir / "users.tsv", rows);
}

void Portal::saveSubjects() const {
    std::vector<storage::Row> rows;
    for (const Subject& s : subjects_) rows.push_back({s.faculty, s.name, std::to_string(s.creditHours)});
    storage::writeTable(options_.dataDir / "subjects.tsv", rows);
}

void Portal::saveEnrollments() const {
    std::vector<storage::Row> rows;
    for (const auto& [who, what] : enrollments_) rows.push_back({who, what});
    storage::writeTable(options_.dataDir / "enrollments.tsv", rows);
}

void Portal::saveAttendance() const {
    std::vector<storage::Row> rows;
    for (const AttendanceRecord& r : attendance_) rows.push_back({r.date, r.subject, r.student, r.present ? "P" : "A"});
    storage::writeTable(options_.dataDir / "attendance.tsv", rows);
}

void Portal::saveSettings() const {
    storage::writeTable(options_.dataDir / "settings.tsv", {{"min_attendance", std::to_string(minAttendance_)}});
}

void Portal::saveMarks() const {
    std::vector<storage::Row> rows;
    for (const Mark& m : marks_) rows.push_back({m.student, m.subject, std::to_string(m.mark)});
    storage::writeTable(options_.dataDir / "marks.tsv", rows);
}

}  // namespace cms
