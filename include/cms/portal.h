#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "cms/crypto.h"

namespace cms {

enum class Role { Admin, Faculty, Student };
std::string roleName(Role role);

// Thrown for anything the person using the portal did wrong, such as a
// taken username or a mark out of range. The message is safe to show.
class PortalError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A salted PBKDF2-SHA256 password hash. The password itself is never stored.
struct PasswordHash {
    std::uint32_t iterations = 0;
    crypto::Bytes salt;
    crypto::Bytes hash;

    static PasswordHash create(const std::string& password, std::uint32_t iterations);
    bool matches(const std::string& password) const;

    // "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>"
    std::string encode() const;
    static std::optional<PasswordHash> decode(const std::string& text);
};

class User {
public:
    User(std::string username, PasswordHash password);
    virtual ~User() = default;

    virtual Role role() const = 0;

    const std::string& username() const { return username_; }
    const PasswordHash& password() const { return password_; }
    void setPassword(PasswordHash password) { password_ = std::move(password); }

    int failedLogins = 0;
    std::int64_t lockedUntil = 0;  // Unix time, 0 when not locked

private:
    std::string username_;
    PasswordHash password_;
};

class Admin final : public User {
public:
    using User::User;
    Role role() const override { return Role::Admin; }
};

class Faculty final : public User {
public:
    using User::User;
    Role role() const override { return Role::Faculty; }
};

class Student final : public User {
public:
    using User::User;
    Role role() const override { return Role::Student; }
};

std::unique_ptr<User> makeUser(Role role, std::string username, PasswordHash password);

struct Subject {
    std::string name;
    int creditHours = 0;
    std::string faculty;  // the faculty member who teaches it
};

struct AttendanceRecord {
    std::string date;  // YYYY-MM-DD
    std::string subject;
    std::string student;
    bool present = false;
};

struct Mark {
    std::string student;
    std::string subject;
    int mark = 0;
};

struct AttendanceSummary {
    std::string subject;
    int attended = 0;
    int total = 0;
};

// One student in one subject whose attendance is below the requirement.
struct AttendanceShortage {
    std::string student;
    std::string subject;
    int attended = 0;
    int total = 0;
};

// Attendance as a whole-number percent, rounded half up. 0 when there were no classes.
int attendancePercent(int attended, int total);

enum class LoginStatus { Ok, WrongCredentials, Locked };

struct LoginResult {
    LoginStatus status = LoginStatus::WrongCredentials;
    const User* user = nullptr;
    std::int64_t secondsLocked = 0;
};

class Portal {
public:
    struct Options {
        std::filesystem::path dataDir = "data";
        std::uint32_t iterations = 600000;  // OWASP guidance for PBKDF2-SHA256
        int maxFailedLogins = 5;
        std::int64_t lockSeconds = 300;
        std::function<std::int64_t()> clock;  // Unix time; defaults to the system clock
    };

    static constexpr int kMinPassword = 8;
    static constexpr int kMaxPassword = 128;
    static constexpr int kMinMark = 0;
    static constexpr int kMaxMark = 100;
    static constexpr int kMinCredits = 1;
    static constexpr int kMaxCredits = 6;
    // Attendance requirement for sitting a subject's exam. The admin can
    // change it within these bounds; 75% is the usual university rule.
    static constexpr int kDefaultMinAttendance = 75;
    static constexpr int kLowestMinAttendance = 50;
    static constexpr int kHighestMinAttendance = 100;

    explicit Portal(Options options);

    // First run: there is no admin yet, and the first one must be created.
    bool needsSetup() const;
    void createFirstAdmin(const std::string& username, const std::string& password);

    void registerUser(Role role, const std::string& username, const std::string& password);
    LoginResult login(Role role, const std::string& username, const std::string& password);
    void changePassword(const User& user, const std::string& current, const std::string& next);

    // Admin
    const User* findUser(Role role, const std::string& username) const;
    std::vector<const User*> users(Role role) const;
    void resetPassword(const User& admin, const std::string& username, const std::string& password);

    // Subjects
    void addSubject(const User& faculty, const std::string& name, int creditHours);
    std::vector<Subject> subjects() const;
    std::vector<Subject> subjectsTaughtBy(const User& faculty) const;
    std::optional<Subject> findSubject(const std::string& name) const;

    // Enrollment
    void enroll(const User& student, const std::string& subject);
    std::vector<std::string> enrolledSubjects(const std::string& student) const;
    std::vector<std::string> studentsIn(const std::string& subject) const;

    // Attendance: one class session per subject per date
    bool hasAttendance(const std::string& subject, const std::string& date) const;
    void recordAttendance(const User& faculty, const std::string& subject, const std::string& date,
                          const std::map<std::string, bool>& present);
    std::vector<AttendanceSummary> attendanceFor(const std::string& student) const;

    // Attendance requirement and exam eligibility
    int minAttendance() const { return minAttendance_; }
    void setMinAttendance(const User& admin, int percent);
    // A student may sit a subject's exam unless classes were held and their
    // attendance is below the requirement.
    bool isEligible(const std::string& student, const std::string& subject) const;
    // Every student and subject below the requirement, lowest attendance first.
    std::vector<AttendanceShortage> shortages() const;
    std::vector<AttendanceRecord> attendanceRecords(const std::string& subject) const;
    // One student's classes in one subject, oldest first.
    std::vector<AttendanceRecord> attendanceHistory(const std::string& student, const std::string& subject) const;

    // Marks
    void setMark(const User& faculty, const std::string& subject, const std::string& student, int mark);
    std::vector<Mark> marksFor(const std::string& student) const;
    std::vector<Mark> marksIn(const std::string& subject) const;

    std::string today() const;
    std::string term() const;  // "Spring 2026", "Summer 2026" or "Fall 2026"

    // Input rules, shared with the menus so they can explain them up front.
    static std::string normalizeUsername(const std::string& username);
    static void checkUsername(const std::string& username);
    static void checkPassword(const std::string& username, const std::string& password);
    static std::string cleanSubjectName(const std::string& name);
    static bool isValidDate(const std::string& date);

private:
    std::int64_t now() const;
    User* findMutable(const std::string& username);
    const Subject& requireOwnSubject(const User& faculty, const std::string& subject) const;
    static void requireRole(const User& user, Role role);

    void load();
    void saveUsers() const;
    void saveSubjects() const;
    void saveEnrollments() const;
    void saveAttendance() const;
    void saveMarks() const;
    void saveSettings() const;

    Options options_;
    std::vector<std::unique_ptr<User>> users_;
    std::vector<Subject> subjects_;
    std::vector<std::pair<std::string, std::string>> enrollments_;  // student, subject
    std::vector<AttendanceRecord> attendance_;
    std::vector<Mark> marks_;
    int minAttendance_ = kDefaultMinAttendance;
    std::optional<PasswordHash> dummy_;  // checked against unknown usernames, see login()
};

}  // namespace cms
