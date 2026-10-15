#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "check.h"
#include "cms/portal.h"

using namespace cms;
namespace fs = std::filesystem;

namespace {

// A portal in its own empty folder, with fast hashing and a clock the test
// controls. 2026-03-02 12:00 UTC.
struct Fixture {
    fs::path dir;
    std::int64_t time = 1772452800;
    std::unique_ptr<Portal> portal;

    explicit Fixture(const std::string& name) : dir(fs::temp_directory_path() / ("cms_test_" + name)) {
        fs::remove_all(dir);
        reopen();
    }
    ~Fixture() { fs::remove_all(dir); }

    // Loads everything again from disk, like restarting the program.
    Portal& reopen() {
        Portal::Options options;
        options.dataDir = dir;
        options.iterations = 1000;
        options.clock = [this] { return time; };
        portal = std::make_unique<Portal>(options);
        return *portal;
    }

    const User& signIn(Role role, const std::string& name, const std::string& password = "correct-horse") {
        LoginResult r = portal->login(role, name, password);
        if (r.status != LoginStatus::Ok) throw std::runtime_error("login failed for " + name);
        return *r.user;
    }

    std::string file(const std::string& name) const {
        std::ifstream in(dir / name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

// One faculty member teaching Data Structures to alice and bob.
struct Classroom : Fixture {
    explicit Classroom(const std::string& name) : Fixture(name) {
        portal->createFirstAdmin("admin", "admin-password");
        portal->registerUser(Role::Faculty, "teacher", "correct-horse");
        portal->registerUser(Role::Student, "alice", "correct-horse");
        portal->registerUser(Role::Student, "bob", "correct-horse");
        portal->addSubject(signIn(Role::Faculty, "teacher"), "Data Structures", 3);
        portal->enroll(signIn(Role::Student, "alice"), "Data Structures");
        portal->enroll(signIn(Role::Student, "bob"), "Data Structures");
    }
};

}  // namespace

// ---------------------------------------------------------------- setup and accounts

TEST("first run needs an admin, and only one can be created that way") {
    Fixture f("setup");
    CHECK(f.portal->needsSetup());
    f.portal->createFirstAdmin("Admin", "admin-password");
    CHECK(!f.portal->needsSetup());
    CHECK(f.portal->findUser(Role::Admin, "admin") != nullptr);
    CHECK_THROWS(PortalError, f.portal->createFirstAdmin("second", "admin-password"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Admin, "sneaky", "admin-password"));
}

TEST("old bug: hardcoded admin/1234 no longer works") {
    Fixture f("hardcoded");
    f.portal->createFirstAdmin("principal", "admin-password");
    CHECK(f.portal->login(Role::Admin, "admin", "1234").status == LoginStatus::WrongCredentials);
}

TEST("old bug: passwords were stored in plain text") {
    Fixture f("plaintext");
    f.portal->registerUser(Role::Student, "alice", "my-secret-pass");
    std::string users = f.file("users.tsv");
    CHECK(users.find("my-secret-pass") == std::string::npos);
    CHECK(users.find("pbkdf2-sha256$1000$") != std::string::npos);
}

TEST("same password gets a different salt and hash for each user") {
    Fixture f("salt");
    f.portal->registerUser(Role::Student, "alice", "same-password");
    f.portal->registerUser(Role::Student, "bob", "same-password");
    const User* a = f.portal->findUser(Role::Student, "alice");
    const User* b = f.portal->findUser(Role::Student, "bob");
    CHECK(a->password().salt != b->password().salt);
    CHECK(a->password().hash != b->password().hash);
}

TEST("old bug: registering an existing username took over the account") {
    Fixture f("takeover");
    f.portal->registerUser(Role::Student, "alice", "alice-password");
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "alice", "attacker-pass"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "ALICE", "attacker-pass"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Faculty, "alice", "attacker-pass"));
    CHECK(f.portal->login(Role::Student, "alice", "attacker-pass").status != LoginStatus::Ok);
    CHECK(f.portal->login(Role::Student, "alice", "alice-password").status == LoginStatus::Ok);
}

TEST("username and password rules") {
    Fixture f("rules");
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "ab", "long-enough"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "has space", "long-enough"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "tab\tname", "long-enough"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "carol", "short"));
    CHECK_THROWS(PortalError, f.portal->registerUser(Role::Student, "carol12345", "CAROL12345"));
    f.portal->registerUser(Role::Student, "Carol.B-2", "pass with spaces");
    CHECK(f.portal->login(Role::Student, "carol.b-2", "pass with spaces").status == LoginStatus::Ok);
}

TEST("login checks the role, the password and the username") {
    Classroom f("login");
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Ok);
    CHECK(f.portal->login(Role::Student, "Alice", "correct-horse").status == LoginStatus::Ok);
    CHECK(f.portal->login(Role::Faculty, "alice", "correct-horse").status == LoginStatus::WrongCredentials);
    CHECK(f.portal->login(Role::Student, "alice", "Correct-horse").status == LoginStatus::WrongCredentials);
    CHECK(f.portal->login(Role::Student, "nobody", "correct-horse").status == LoginStatus::WrongCredentials);
}

TEST("five wrong passwords lock the account, which unlocks later") {
    Classroom f("lockout");
    for (int i = 0; i < 4; ++i) {
        CHECK(f.portal->login(Role::Student, "alice", "wrong").status == LoginStatus::WrongCredentials);
    }
    LoginResult locked = f.portal->login(Role::Student, "alice", "wrong");
    CHECK(locked.status == LoginStatus::Locked);
    CHECK_EQ(locked.secondsLocked, std::int64_t(300));

    // The right password doesn't help while locked, even after a restart.
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Locked);
    f.reopen();
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Locked);

    f.time += 301;
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Ok);
    // Other accounts were never affected.
    CHECK(f.portal->login(Role::Student, "bob", "correct-horse").status == LoginStatus::Ok);
}

TEST("a successful login resets the wrong-password count") {
    Classroom f("reset_count");
    for (int i = 0; i < 4; ++i) f.portal->login(Role::Student, "alice", "wrong");
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Ok);
    for (int i = 0; i < 4; ++i) f.portal->login(Role::Student, "alice", "wrong");
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Ok);
}

TEST("admin resets a forgotten password and unlocks the account") {
    Classroom f("admin_reset");
    for (int i = 0; i < 5; ++i) f.portal->login(Role::Student, "alice", "wrong");
    const User& admin = f.signIn(Role::Admin, "admin", "admin-password");
    f.portal->resetPassword(admin, "alice", "temporary-pass");
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::WrongCredentials);
    CHECK(f.portal->login(Role::Student, "alice", "temporary-pass").status == LoginStatus::Ok);
}

TEST("only an admin can reset passwords") {
    Classroom f("reset_role");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    CHECK_THROWS(PortalError, f.portal->resetPassword(teacher, "alice", "taken-over!"));
    CHECK(f.portal->login(Role::Student, "alice", "correct-horse").status == LoginStatus::Ok);
}

TEST("changing your own password needs the current one") {
    Classroom f("change");
    const User& alice = f.signIn(Role::Student, "alice");
    CHECK_THROWS(PortalError, f.portal->changePassword(alice, "wrong", "new-password"));
    f.portal->changePassword(alice, "correct-horse", "new-password");
    CHECK(f.portal->login(Role::Student, "alice", "new-password").status == LoginStatus::Ok);
}

TEST("accounts survive a restart") {
    Classroom f("restart");
    f.reopen();
    CHECK(!f.portal->needsSetup());
    CHECK(f.portal->login(Role::Student, "bob", "correct-horse").status == LoginStatus::Ok);
    CHECK_EQ(f.portal->users(Role::Student).size(), std::size_t(2));
}

// ---------------------------------------------------------------- subjects

TEST("old bug: subject names with spaces broke the data files") {
    Classroom f("spaces");
    f.reopen();
    std::optional<Subject> s = f.portal->findSubject("data structures");
    CHECK(s.has_value());
    CHECK_EQ(s->name, std::string("Data Structures"));
    CHECK_EQ(s->creditHours, 3);
    CHECK_EQ(f.portal->studentsIn("Data Structures"), (std::vector<std::string>{"alice", "bob"}));
}

TEST("subject rules: unique names, credit hours, faculty only") {
    Classroom f("subject_rules");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    CHECK_THROWS(PortalError, f.portal->addSubject(teacher, "  data   STRUCTURES ", 3));
    CHECK_THROWS(PortalError, f.portal->addSubject(teacher, "Networks", 0));
    CHECK_THROWS(PortalError, f.portal->addSubject(teacher, "Networks", 7));
    CHECK_THROWS(PortalError, f.portal->addSubject(teacher, "x", 3));
    CHECK_THROWS(PortalError, f.portal->addSubject(f.signIn(Role::Student, "alice"), "Networks", 3));
    f.portal->addSubject(teacher, "  Computer   Networks ", 4);
    CHECK_EQ(f.portal->findSubject("computer networks")->name, std::string("Computer Networks"));
}

TEST("students can't register twice or for missing subjects") {
    Classroom f("enroll");
    const User& alice = f.signIn(Role::Student, "alice");
    CHECK_THROWS(PortalError, f.portal->enroll(alice, "Data Structures"));
    CHECK_THROWS(PortalError, f.portal->enroll(alice, "Astrology"));
    CHECK_THROWS(PortalError, f.portal->enroll(f.signIn(Role::Faculty, "teacher"), "Data Structures"));
}

// ---------------------------------------------------------------- attendance

TEST("old bug: marking attendance erased all earlier records") {
    Classroom f("attendance_keep");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-01", {{"alice", true}, {"bob", false}});
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-02", {{"alice", true}, {"bob", true}});
    f.reopen();

    std::vector<AttendanceSummary> alice = f.portal->attendanceFor("alice");
    CHECK_EQ(alice.size(), std::size_t(1));
    CHECK_EQ(alice[0].attended, 2);
    CHECK_EQ(alice[0].total, 2);
    std::vector<AttendanceSummary> bob = f.portal->attendanceFor("bob");
    CHECK_EQ(bob[0].attended, 1);
    CHECK_EQ(bob[0].total, 2);
}

TEST("re-marking the same date replaces only that session") {
    Classroom f("attendance_replace");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-01", {{"alice", false}, {"bob", false}});
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-02", {{"alice", true}, {"bob", true}});
    CHECK(f.portal->hasAttendance("Data Structures", "2026-03-01"));
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-01", {{"alice", true}, {"bob", false}});
    CHECK_EQ(f.portal->attendanceRecords("Data Structures").size(), std::size_t(4));
    CHECK_EQ(f.portal->attendanceFor("alice")[0].attended, 2);
    CHECK_EQ(f.portal->attendanceFor("bob")[0].attended, 1);
}

TEST("attendance rules: own subject, enrolled students, everyone, real past dates") {
    Classroom f("attendance_rules");
    f.portal->registerUser(Role::Faculty, "other", "correct-horse");
    f.portal->registerUser(Role::Student, "carol", "correct-horse");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    const User& other = f.signIn(Role::Faculty, "other");
    std::map<std::string, bool> both = {{"alice", true}, {"bob", true}};

    CHECK_THROWS(PortalError, f.portal->recordAttendance(other, "Data Structures", "2026-03-01", both));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(f.signIn(Role::Student, "alice"), "Data Structures", "2026-03-01", both));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(teacher, "Data Structures", "2026-03-01", {{"alice", true}}));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(teacher, "Data Structures", "2026-03-01",
                                                         {{"alice", true}, {"bob", true}, {"carol", true}}));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(teacher, "Data Structures", "2026-02-30", both));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(teacher, "Data Structures", "01-03-2026", both));
    CHECK_THROWS(PortalError, f.portal->recordAttendance(teacher, "Data Structures", "2026-03-09", both));
    CHECK(f.portal->attendanceRecords("Data Structures").empty());
}

TEST("date validation") {
    CHECK(Portal::isValidDate("2024-02-29"));
    CHECK(!Portal::isValidDate("2023-02-29"));
    CHECK(Portal::isValidDate("2000-02-29"));
    CHECK(!Portal::isValidDate("2100-02-29"));
    CHECK(!Portal::isValidDate("2026-13-01"));
    CHECK(!Portal::isValidDate("2026-04-31"));
    CHECK(!Portal::isValidDate("2026-4-1"));
    CHECK(!Portal::isValidDate("abcd-ef-gh"));
}

// ---------------------------------------------------------------- marks

TEST("old bug: marks out of range were accepted") {
    Classroom f("marks_range");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    CHECK_THROWS(PortalError, f.portal->setMark(teacher, "Data Structures", "alice", 999));
    CHECK_THROWS(PortalError, f.portal->setMark(teacher, "Data Structures", "alice", -1));
    f.portal->setMark(teacher, "Data Structures", "alice", 100);
    f.portal->setMark(teacher, "Data Structures", "bob", 0);
    CHECK_EQ(f.portal->marksIn("Data Structures").size(), std::size_t(2));
}

TEST("old bug: marks could be given for a subject the student never took") {
    Classroom f("marks_enrolled");
    f.portal->registerUser(Role::Student, "carol", "correct-horse");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    CHECK_THROWS(PortalError, f.portal->setMark(teacher, "Data Structures", "carol", 80));
    CHECK_THROWS(PortalError, f.portal->setMark(teacher, "Data Structures", "ghost", 80));
    CHECK(f.portal->marksFor("carol").empty());
}

TEST("only the subject's own teacher can give marks") {
    Classroom f("marks_owner");
    f.portal->registerUser(Role::Faculty, "other", "correct-horse");
    CHECK_THROWS(PortalError, f.portal->setMark(f.signIn(Role::Faculty, "other"), "Data Structures", "alice", 90));
    CHECK_THROWS(PortalError, f.portal->setMark(f.signIn(Role::Student, "alice"), "Data Structures", "alice", 100));
    CHECK(f.portal->marksFor("alice").empty());
}

TEST("a new mark replaces the old one and survives a restart") {
    Classroom f("marks_update");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    f.portal->setMark(teacher, "Data Structures", "alice", 70);
    f.portal->setMark(teacher, "data structures", "ALICE", 85);
    f.reopen();
    std::vector<Mark> marks = f.portal->marksFor("alice");
    CHECK_EQ(marks.size(), std::size_t(1));
    CHECK_EQ(marks[0].mark, 85);
    CHECK_EQ(marks[0].subject, std::string("Data Structures"));
}

// ---------------------------------------------------------------- files

TEST("broken lines in data files are skipped, not trusted") {
    Fixture f("corrupt");
    fs::create_directories(f.dir);
    {
        std::ofstream users(f.dir / "users.tsv");
        users << "student\tmallory\tplaintext-password\t0\t0\n";      // not a hash
        users << "superuser\troot\tpbkdf2-sha256$1$00$00\t0\t0\n";     // unknown role
        users << "student\tshort\n";                                    // too few fields
    }
    {
        std::ofstream marks(f.dir / "marks.tsv");
        marks << "alice\tMath\tninety\n";
    }
    f.reopen();
    CHECK(f.portal->needsSetup());
    CHECK(f.portal->users(Role::Student).empty());
    CHECK(f.portal->marksFor("alice").empty());
    CHECK(f.portal->login(Role::Student, "mallory", "plaintext-password").status != LoginStatus::Ok);
}

TEST("password hash encoding round trip and bad input") {
    PasswordHash h = PasswordHash::create("hello-world", 10);
    std::optional<PasswordHash> back = PasswordHash::decode(h.encode());
    CHECK(back.has_value());
    CHECK(back->matches("hello-world"));
    CHECK(!back->matches("hello-World"));
    CHECK(!PasswordHash::decode("md5$1$00$00").has_value());
    CHECK(!PasswordHash::decode("pbkdf2-sha256$0$00$00").has_value());
    CHECK(!PasswordHash::decode("pbkdf2-sha256$10$zz$00").has_value());
    CHECK(!PasswordHash::decode("pbkdf2-sha256$10$00").has_value());
}

TEST("attendance history is per student and in date order") {
    Classroom f("history");
    const User& teacher = f.signIn(Role::Faculty, "teacher");
    f.portal->recordAttendance(teacher, "Data Structures", "2026-03-02", {{"alice", false}, {"bob", true}});
    f.portal->recordAttendance(teacher, "Data Structures", "2026-02-27", {{"alice", true}, {"bob", true}});
    std::vector<AttendanceRecord> alice = f.portal->attendanceHistory("alice", "Data Structures");
    CHECK_EQ(alice.size(), std::size_t(2));
    CHECK_EQ(alice[0].date, std::string("2026-02-27"));
    CHECK(alice[0].present);
    CHECK(!alice[1].present);
    CHECK(f.portal->attendanceHistory("carol", "Data Structures").empty());
}

TEST("term name follows the month") {
    Fixture f("term");
    f.time = 1772452800;  // 2 Mar 2026
    CHECK_EQ(f.portal->term(), std::string("Spring 2026"));
    f.time = 1782907200;  // 1 Jul 2026
    CHECK_EQ(f.portal->term(), std::string("Summer 2026"));
    f.time = 1791720000;  // 11 Oct 2026
    CHECK_EQ(f.portal->term(), std::string("Fall 2026"));
}
