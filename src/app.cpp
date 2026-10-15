#include "cms/app.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>

namespace cms {

namespace c = console;

namespace {

std::string titleCase(const std::string& word) {
    std::string out = word;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::string passwordRules() {
    return "Password: at least " + std::to_string(Portal::kMinPassword) +
           " characters, and not the same as the username.";
}

// "86%", or red when below the attendance requirement.
std::string rate(int part, int whole, int limit) {
    if (whole == 0) return c::dim("-");
    int p = attendancePercent(part, whole);
    std::string text = std::to_string(p) + "%";
    return p < limit ? c::red(text) : text;
}

// Exam eligibility for one student in one subject.
std::string exam(int part, int whole, int limit) {
    if (whole == 0) return c::dim("-");
    return attendancePercent(part, whole) < limit ? c::red("Not eligible") : "Eligible";
}

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string lastClasses(const std::vector<AttendanceRecord>& history, std::size_t count = 7) {
    std::vector<bool> present;
    std::size_t start = history.size() > count ? history.size() - count : 0;
    for (std::size_t i = start; i < history.size(); ++i) present.push_back(history[i].present);
    return "  " + c::dots(present);  // lines up under the "  Last 7" heading
}

std::string credits(int hours) { return std::to_string(hours); }

}  // namespace

// ---------------------------------------------------------------- dashboard

void Dashboard::run() {
    std::vector<c::Tab> items = tabs();
    int active = items.front().key;
    bool firstScreen = true;
    while (true) {
        c::clear();
        c::header(c::bold(user_.username()) + c::dim("  ·  ") + titleCase(roleName(user_.role())), portal_.term());
        c::tabs(items, active);
        if (firstScreen) {
            std::vector<std::string> warnings = alerts();
            for (const std::string& w : warnings) c::warn(w);
            if (!warnings.empty()) std::cout << "\n";
            firstScreen = false;
        }
        try {
            show(active);
        } catch (const PortalError& e) {
            c::fail(e.what());
        }
        int next = c::chooseTab(items);
        if (next == 0) return;
        active = next;
    }
}

void Dashboard::account() {
    c::section("Account");
    c::table({{"Username", 24}, {"Role", 12}}, {{user_.username(), titleCase(roleName(user_.role()))}});
    std::cout << "\n";
    if (!c::confirm("Change your password?")) return;
    changePassword();
}

void Dashboard::changePassword() {
    std::cout << "\n";
    std::string current = c::readPassword("Current password: ");
    c::hint(passwordRules());
    std::string next = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != next) throw PortalError("The new passwords don't match.");
    portal_.changePassword(user_, current, next);
    c::ok("Password changed.");
}

std::string Dashboard::pickSubject(const std::vector<Subject>& subjects) {
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        rows.push_back({std::to_string(i + 1), subjects[i].name, std::to_string(portal_.studentsIn(subjects[i].name).size())});
    }
    c::table({{"#", 3, true}, {"Subject", 30}, {"Students", 8, true}}, rows);
    std::cout << "\n";
    int choice = c::readInt("Subject number (0 to cancel): ", 0, static_cast<int>(subjects.size()));
    return choice == 0 ? "" : subjects[choice - 1].name;
}

// ---------------------------------------------------------------- admin

std::vector<c::Tab> AdminDashboard::tabs() const {
    return {{1, "Faculty"}, {2, "Students"}, {3, "At risk"}, {4, "Find"}, {5, "Reset"}, {6, "Settings"}, {0, "Log out"}};
}

void AdminDashboard::show(int tab) {
    switch (tab) {
        case 1: facultyList(); break;
        case 2: studentList(); break;
        case 3: atRisk(); break;
        case 4: findAccount(); break;
        case 5: resetPassword(); break;
        default: settings(); break;
    }
}

std::vector<std::string> AdminDashboard::alerts() const {
    std::vector<AttendanceShortage> list = portal_.shortages();
    if (list.empty()) return {};
    std::vector<std::string> students;
    for (const AttendanceShortage& s : list) {
        if (std::find(students.begin(), students.end(), s.student) == students.end()) students.push_back(s.student);
    }
    return {plural(students.size(), "student is", "students are") + " below the " +
            std::to_string(portal_.minAttendance()) + "% attendance requirement. See tab 3."};
}

void AdminDashboard::atRisk() {
    c::section("At risk");
    c::hint("Below the " + std::to_string(portal_.minAttendance()) + "% attendance requirement, so they can't sit for that exam.");
    std::cout << "\n";
    std::vector<AttendanceShortage> list = portal_.shortages();
    if (list.empty()) {
        c::ok("Every student meets the attendance requirement.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const AttendanceShortage& s : list) {
        rows.push_back({s.student, s.subject, std::to_string(s.attended) + " / " + std::to_string(s.total),
                        rate(s.attended, s.total, portal_.minAttendance()), c::red("Not eligible")});
    }
    c::table({{"Student", 14}, {"Subject", 22}, {"Classes", 7, true}, {"Rate", 5, true}, {"Exam", 12}}, rows);
}

void AdminDashboard::settings() {
    c::section("Settings");
    c::table({{"#", 3, true}, {"Setting", 26}, {"Value", 12}},
             {{"1", "Attendance requirement", std::to_string(portal_.minAttendance()) + "%"},
              {"2", "My password", user_.username()}});
    std::cout << "\n";
    int choice = c::readInt("Setting to change (0 for none): ", 0, 2);
    if (choice == 1) {
        c::hint("Students below this can't sit for the exam. Allowed: " +
                std::to_string(Portal::kLowestMinAttendance) + "% to " + std::to_string(Portal::kHighestMinAttendance) + "%.");
        int value = c::readInt("New requirement in %: ", Portal::kLowestMinAttendance, Portal::kHighestMinAttendance);
        portal_.setMinAttendance(user_, value);
        std::cout << "\n";
        c::ok("Attendance requirement set to " + std::to_string(value) + "%.");
    } else if (choice == 2) {
        changePassword();
    }
}

void AdminDashboard::facultyList() {
    c::section("Faculty");
    std::vector<const User*> list = portal_.users(Role::Faculty);
    if (list.empty()) {
        c::hint("No faculty accounts yet.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const User* u : list) {
        std::vector<Subject> taught = portal_.subjectsTaughtBy(*u);
        std::size_t students = 0;
        for (const Subject& s : taught) students += portal_.studentsIn(s.name).size();
        rows.push_back({u->username(), std::to_string(taught.size()), std::to_string(students)});
    }
    c::table({{"Username", 26}, {"Subjects", 8, true}, {"Students", 8, true}}, rows);
}

void AdminDashboard::studentList() {
    c::section("Students");
    std::vector<const User*> list = portal_.users(Role::Student);
    if (list.empty()) {
        c::hint("No student accounts yet.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    int low = 0;
    for (const User* u : list) {
        int here = 0, total = 0;
        for (const AttendanceSummary& a : portal_.attendanceFor(u->username())) {
            here += a.attended;
            total += a.total;
        }
        if (total && attendancePercent(here, total) < portal_.minAttendance()) ++low;
        rows.push_back({u->username(), std::to_string(portal_.enrolledSubjects(u->username()).size()), rate(here, total, portal_.minAttendance())});
    }
    c::table({{"Username", 26}, {"Subjects", 8, true}, {"Attendance", 10, true}}, rows);
    if (low) {
        std::cout << "\n";
        c::warn(std::to_string(low) + (low == 1 ? " student is" : " students are") + " below " +
                std::to_string(portal_.minAttendance()) + "% attendance overall.");
    }
}

void AdminDashboard::findAccount() {
    c::section("Find an account");
    std::string name = c::readLine("Username: ");
    std::cout << "\n";
    for (Role role : {Role::Admin, Role::Faculty, Role::Student}) {
        const User* user = portal_.findUser(role, name);
        if (!user) continue;
        c::ok(user->username() + " is a registered " + roleName(role) + ".");
        std::cout << "\n";
        if (role == Role::Faculty) {
            std::vector<std::vector<std::string>> rows;
            for (const Subject& s : portal_.subjectsTaughtBy(*user)) {
                rows.push_back({s.name, credits(s.creditHours), std::to_string(portal_.studentsIn(s.name).size())});
            }
            if (rows.empty()) c::hint("Teaches no subjects yet.");
            else c::table({{"Teaches", 30}, {"Credits", 7, true}, {"Students", 8, true}}, rows);
        } else if (role == Role::Student) {
            std::vector<std::vector<std::string>> rows;
            for (const AttendanceSummary& a : portal_.attendanceFor(user->username())) {
                rows.push_back({a.subject, rate(a.attended, a.total, portal_.minAttendance())});
            }
            if (rows.empty()) c::hint("Not registered for any subjects yet.");
            else c::table({{"Registered for", 30}, {"Attendance", 10, true}}, rows);
        }
        return;
    }
    c::fail("No account named \"" + name + "\".");
}

void AdminDashboard::resetPassword() {
    c::section("Reset a password");
    c::hint("Use this when someone forgets their password. It also unlocks the account.");
    std::cout << "\n";
    std::string name = c::readLine("Username (Enter to cancel): ");
    if (name.empty()) return;
    c::hint(passwordRules());
    std::string password = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != password) throw PortalError("The passwords don't match.");
    portal_.resetPassword(user_, name, password);
    c::ok("Password reset. Give the new password to " + Portal::normalizeUsername(name) + " privately.");
}

// ---------------------------------------------------------------- faculty

std::vector<c::Tab> FacultyDashboard::tabs() const {
    return {{1, "Subjects"}, {2, "Add"}, {3, "Attendance"}, {4, "Marks"}, {5, "Report"}, {6, "Account"}, {0, "Log out"}};
}

void FacultyDashboard::show(int tab) {
    switch (tab) {
        case 1: mySubjects(); break;
        case 2: addSubject(); break;
        case 3: markAttendance(); break;
        case 4: giveMarks(); break;
        case 5: classReport(); break;
        default: account(); break;
    }
}

std::vector<std::string> FacultyDashboard::alerts() const {
    std::size_t count = 0;
    for (const AttendanceShortage& s : portal_.shortages()) {
        std::optional<Subject> subject = portal_.findSubject(s.subject);
        if (subject && subject->faculty == user_.username()) ++count;
    }
    if (count == 0) return {};
    return {plural(count, "student", "students") + " in your subjects " + (count == 1 ? "is" : "are") + " below " +
            std::to_string(portal_.minAttendance()) + "% attendance. See tab 5."};
}

void FacultyDashboard::mySubjects() {
    c::section("My subjects");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) {
        c::hint("You haven't added any subjects yet. Use tab 2 to add one.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    std::size_t students = 0;
    for (const Subject& s : subjects) {
        std::size_t n = portal_.studentsIn(s.name).size();
        students += n;
        std::map<std::string, bool> dates;  // one class per date
        for (const AttendanceRecord& r : portal_.attendanceRecords(s.name)) dates[r.date] = true;
        rows.push_back({s.name, credits(s.creditHours), std::to_string(n), std::to_string(dates.size())});
    }
    c::table({{"Subject", 30}, {"Credits", 7, true}, {"Students", 8, true}, {"Classes held", 12, true}}, rows,
             {"Total", "", std::to_string(students), ""});
}

void FacultyDashboard::addSubject() {
    c::section("Add a subject");
    std::string name = c::readLine("Subject name (Enter to cancel): ");
    if (name.empty()) return;
    std::string clean = Portal::cleanSubjectName(name);
    int hours = c::readInt("Credit hours (" + std::to_string(Portal::kMinCredits) + "-" +
                               std::to_string(Portal::kMaxCredits) + "): ",
                           Portal::kMinCredits, Portal::kMaxCredits);
    portal_.addSubject(user_, clean, hours);
    std::cout << "\n";
    c::ok("Added " + clean + ". Students can register for it now.");
}

void FacultyDashboard::markAttendance() {
    c::section("Mark attendance");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects);
    if (subject.empty()) return;

    std::vector<std::string> students = portal_.studentsIn(subject);
    if (students.empty()) throw PortalError("No students are registered for " + subject + " yet.");

    std::string today = portal_.today();
    std::string date = c::readLine("Date as YYYY-MM-DD (Enter for today, " + today + "): ");
    if (date.empty()) date = today;
    if (!Portal::isValidDate(date)) throw PortalError("Date must be a real date written as YYYY-MM-DD.");
    if (date > today) throw PortalError("Attendance can't be marked for a future date.");
    if (portal_.hasAttendance(subject, date) &&
        !c::confirm("Attendance for " + subject + " on " + date + " already exists. Replace it?")) {
        return;
    }

    std::cout << "\n";
    c::hint("Type P for present or A for absent.");
    std::map<std::string, bool> present;
    for (const std::string& student : students) {
        while (true) {
            std::string answer = c::readLine(student + std::string(student.size() < 18 ? 18 - student.size() : 1, ' '));
            if (answer == "P" || answer == "p") { present[student] = true; break; }
            if (answer == "A" || answer == "a") { present[student] = false; break; }
            c::fail("Type P or A.");
        }
    }
    portal_.recordAttendance(user_, subject, date, present);

    int here = 0;
    for (const auto& [_, p] : present) here += p ? 1 : 0;
    std::cout << "\n";
    c::ok("Saved " + subject + " on " + date + ": " + std::to_string(here) + " of " +
          std::to_string(present.size()) + " present.");
}

void FacultyDashboard::giveMarks() {
    c::section("Give marks");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects);
    if (subject.empty()) return;

    std::vector<std::string> students = portal_.studentsIn(subject);
    if (students.empty()) throw PortalError("No students are registered for " + subject + " yet.");
    std::map<std::string, int> current;
    for (const Mark& m : portal_.marksIn(subject)) current[m.student] = m.mark;

    std::cout << "\n";
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 0; i < students.size(); ++i) {
        auto it = current.find(students[i]);
        rows.push_back({std::to_string(i + 1), students[i], it == current.end() ? c::dim("-") : std::to_string(it->second)});
    }
    c::table({{"#", 3, true}, {"Student", 24}, {"Mark", 5, true}}, rows);
    std::cout << "\n";
    int choice = c::readInt("Student number (0 to cancel): ", 0, static_cast<int>(students.size()));
    if (choice == 0) return;

    const std::string& student = students[choice - 1];
    int mark = c::readInt("Mark for " + student + " (" + std::to_string(Portal::kMinMark) + "-" +
                              std::to_string(Portal::kMaxMark) + "): ",
                          Portal::kMinMark, Portal::kMaxMark);
    portal_.setMark(user_, subject, student, mark);
    std::cout << "\n";
    c::ok("Saved: " + student + " got " + std::to_string(mark) + " in " + subject + ".");
}

void FacultyDashboard::classReport() {
    c::section("Class report");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects);
    if (subject.empty()) return;

    std::vector<std::string> students = portal_.studentsIn(subject);
    std::cout << "\n";
    if (students.empty()) {
        c::hint("No students are registered for " + subject + " yet.");
        return;
    }
    std::map<std::string, int> marks;
    for (const Mark& m : portal_.marksIn(subject)) marks[m.student] = m.mark;

    std::vector<std::vector<std::string>> rows;
    int allHere = 0, allTotal = 0, markSum = 0, marked = 0, low = 0;
    for (const std::string& s : students) {
        std::vector<AttendanceRecord> history = portal_.attendanceHistory(s, subject);
        int here = 0;
        for (const AttendanceRecord& r : history) here += r.present ? 1 : 0;
        int total = static_cast<int>(history.size());
        allHere += here;
        allTotal += total;
        if (total && attendancePercent(here, total) < portal_.minAttendance()) ++low;
        auto it = marks.find(s);
        if (it != marks.end()) {
            markSum += it->second;
            ++marked;
        }
        rows.push_back({s, std::to_string(here) + " / " + std::to_string(total), rate(here, total, portal_.minAttendance()),
                        it == marks.end() ? c::dim("-") : std::to_string(it->second), lastClasses(history),
                        exam(here, total, portal_.minAttendance())});
    }
    std::string average = marked ? std::to_string((markSum * 10 / marked + 5) / 10) : "-";
    c::table({{"Student", 14}, {"Classes", 7, true}, {"Rate", 5, true}, {"Mark", 4, true}, {"  Last 7", 9}, {"Exam", 12}}, rows,
             {"Class", std::to_string(allHere) + " / " + std::to_string(allTotal), rate(allHere, allTotal, portal_.minAttendance()), average, "", ""});
    if (low) {
        std::cout << "\n";
        c::warn(plural(static_cast<std::size_t>(low), "student is", "students are") + " below " +
                std::to_string(portal_.minAttendance()) + "% attendance and can't sit for the exam.");
    }
}

// ---------------------------------------------------------------- student

std::vector<c::Tab> StudentDashboard::tabs() const {
    return {{1, "Subjects"}, {2, "Register"}, {3, "Marks"}, {4, "Attendance"}, {5, "Account"}, {0, "Log out"}};
}

void StudentDashboard::show(int tab) {
    switch (tab) {
        case 1: mySubjects(); break;
        case 2: registerSubject(); break;
        case 3: myMarks(); break;
        case 4: myAttendance(); break;
        default: account(); break;
    }
}

std::vector<std::string> StudentDashboard::alerts() const {
    std::vector<std::string> out;
    for (const AttendanceShortage& s : portal_.shortages()) {
        if (s.student != user_.username()) continue;
        out.push_back("Warning: " + s.subject + " attendance is " + std::to_string(attendancePercent(s.attended, s.total)) +
                      "%. You need " + std::to_string(portal_.minAttendance()) + "% to sit for the exam.");
    }
    return out;
}

void StudentDashboard::mySubjects() {
    c::section("My subjects");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    if (mine.empty()) {
        c::hint("You haven't registered for any subjects yet. Use tab 2 to register.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    int total = 0;
    for (const std::string& name : mine) {
        std::optional<Subject> s = portal_.findSubject(name);
        int hours = s ? s->creditHours : 0;
        total += hours;
        rows.push_back({name, credits(hours), s ? s->faculty : "-"});
    }
    c::table({{"Subject", 30}, {"Credits", 7, true}, {"  Teacher", 20}}, rows, {"Total", std::to_string(total), ""});
}

void StudentDashboard::registerSubject() {
    c::section("Register for a subject");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    std::vector<Subject> open;
    for (const Subject& s : portal_.subjects()) {
        bool taken = false;
        for (const std::string& m : mine) taken = taken || m == s.name;
        if (!taken) open.push_back(s);
    }
    if (open.empty()) {
        c::hint("There are no new subjects to register for.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (std::size_t i = 0; i < open.size(); ++i) {
        rows.push_back({std::to_string(i + 1), open[i].name, credits(open[i].creditHours), open[i].faculty});
    }
    c::table({{"#", 3, true}, {"Subject", 30}, {"Credits", 7, true}, {"  Teacher", 18}}, rows);
    std::cout << "\n";
    int choice = c::readInt("Subject number (0 to cancel): ", 0, static_cast<int>(open.size()));
    if (choice == 0) return;
    portal_.enroll(user_, open[choice - 1].name);
    std::cout << "\n";
    c::ok("You are registered for " + open[choice - 1].name + ".");
}

void StudentDashboard::myMarks() {
    c::section("Marks");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    if (mine.empty()) {
        c::hint("You haven't registered for any subjects yet.");
        return;
    }
    std::map<std::string, int> marks;
    for (const Mark& m : portal_.marksFor(user_.username())) marks[m.subject] = m.mark;
    std::vector<std::vector<std::string>> rows;
    int sum = 0, count = 0;
    for (const std::string& subject : mine) {
        std::optional<Subject> s = portal_.findSubject(subject);
        auto it = marks.find(subject);
        if (it != marks.end()) {
            sum += it->second;
            ++count;
        }
        rows.push_back({subject, credits(s ? s->creditHours : 0),
                        it == marks.end() ? c::dim("not marked") : std::to_string(it->second) + " / 100"});
    }
    std::string average = count ? std::to_string((sum * 10 / count + 5) / 10) + " / 100" : "-";
    c::table({{"Subject", 30}, {"Credits", 7, true}, {"Mark", 12, true}}, rows, {"Average", "", average});
}

void StudentDashboard::myAttendance() {
    c::section("Attendance");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    if (mine.empty()) {
        c::hint("You haven't registered for any subjects yet.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> low;
    int allHere = 0, allTotal = 0;
    for (const std::string& subject : mine) {
        std::vector<AttendanceRecord> history = portal_.attendanceHistory(user_.username(), subject);
        int here = 0;
        for (const AttendanceRecord& r : history) here += r.present ? 1 : 0;
        int total = static_cast<int>(history.size());
        allHere += here;
        allTotal += total;
        if (total && attendancePercent(here, total) < portal_.minAttendance()) {
            low.push_back(subject + " attendance is " + std::to_string(attendancePercent(here, total)) + "%.");
        }
        rows.push_back({subject, total ? std::to_string(here) + " / " + std::to_string(total) : c::dim("none yet"),
                        rate(here, total, portal_.minAttendance()), lastClasses(history), exam(here, total, portal_.minAttendance())});
    }
    c::table({{"Subject", 22}, {"Classes", 8, true}, {"Rate", 5, true}, {"  Last 7", 9}, {"Exam", 12}}, rows,
             {"Overall", allTotal ? std::to_string(allHere) + " / " + std::to_string(allTotal) : "-", rate(allHere, allTotal, portal_.minAttendance()), "", ""});
    if (!low.empty()) std::cout << "\n";
    for (const std::string& line : low) {
        c::warn(line + " You need " + std::to_string(portal_.minAttendance()) + "% to sit for the exam.");
    }
}

// ---------------------------------------------------------------- app

int App::run() {
    try {
        if (portal_.needsSetup()) setup();
        const std::vector<c::Tab> portals = {{1, "Admin"}, {2, "Faculty"}, {3, "Student"}, {0, "Exit"}};
        while (true) {
            c::clear();
            c::header("Welcome", portal_.term());
            c::tabs(portals, -1);
            c::section("Sign in");
            c::hint("Open your portal to log in, or register if you are new.");
            int choice = c::chooseTab(portals, "Choose a portal");
            if (choice == 0) break;
            portalScreen(choice == 1 ? Role::Admin : choice == 2 ? Role::Faculty : Role::Student);
        }
    } catch (const c::InputClosed&) {
        std::cout << "\n";
    }
    std::cout << "\n  Goodbye.\n\n";
    return 0;
}

void App::setup() {
    c::clear();
    c::header("First run setup", portal_.term());
    c::section("Create the admin account");
    c::hint("There is no admin account yet. The admin can look up users and reset passwords.");
    std::cout << "\n";
    while (portal_.needsSetup()) {
        try {
            std::string name = c::readLine("Admin username: ");
            c::hint(passwordRules());
            std::string password = c::readPassword("Admin password: ");
            if (c::readPassword("Repeat password: ") != password) throw PortalError("The passwords don't match.");
            portal_.createFirstAdmin(name, password);
        } catch (const PortalError& e) {
            c::fail(e.what());
            std::cout << "\n";
        }
    }
}

void App::portalScreen(Role role) {
    std::vector<c::Tab> items = {{1, "Log in"}};
    if (role != Role::Admin) items.push_back({2, "Register"});  // admin accounts can't be self-registered
    items.push_back({3, "Forgot password"});
    items.push_back({0, "Back"});

    int active = -1;
    while (true) {
        c::clear();
        c::header(titleCase(roleName(role)) + " portal", portal_.term());
        c::tabs(items, active);
        try {
            if (active == 1 && login(role)) {
                active = -1;  // signed out: show the portal screen fresh
                continue;
            }
            if (active == 2) registerAccount(role);
            if (active == 3) forgotPassword();
            if (active == -1) {
                c::section(titleCase(roleName(role)) + " portal");
                c::hint(role == Role::Admin ? "Log in with the admin account." : "Log in, or register if you are new.");
            }
        } catch (const PortalError& e) {
            c::fail(e.what());
        }
        int next = c::chooseTab(items);
        if (next == 0) return;
        active = next;
    }
}

bool App::login(Role role) {
    c::section("Log in");
    std::string name = c::readLine("Username: ");
    std::string password = c::readPassword("Password: ");
    LoginResult result = portal_.login(role, name, password);

    if (result.status == LoginStatus::Locked) {
        std::int64_t minutes = (result.secondsLocked + 59) / 60;
        std::cout << "\n";
        c::fail("Too many wrong passwords. Try again in " + std::to_string(minutes) +
                (minutes == 1 ? " minute." : " minutes."));
        return false;
    }
    if (result.status != LoginStatus::Ok) {
        std::cout << "\n";
        c::fail("Wrong username or password.");
        return false;
    }
    switch (role) {
        case Role::Admin: AdminDashboard(portal_, *result.user).run(); break;
        case Role::Faculty: FacultyDashboard(portal_, *result.user).run(); break;
        case Role::Student: StudentDashboard(portal_, *result.user).run(); break;
    }
    return true;
}

void App::registerAccount(Role role) {
    c::section("Register");
    c::hint("Username: 3 to 32 letters, digits, '.', '_' or '-'.");
    std::string name = c::readLine("Username: ");
    Portal::checkUsername(Portal::normalizeUsername(name));
    c::hint(passwordRules());
    std::string password = c::readPassword("Password: ");
    if (c::readPassword("Repeat password: ") != password) throw PortalError("The passwords don't match.");
    portal_.registerUser(role, name, password);
    std::cout << "\n";
    c::ok("Account created. Choose 1 to log in.");
}

void App::forgotPassword() {
    c::section("Forgot password");
    std::cout << "  Passwords are stored as salted one-way hashes, so nobody can look\n"
                 "  yours up, not even the admin.\n\n"
                 "  Ask the admin to reset it. They will give you a new password, which\n"
                 "  you can change in the Account tab after logging in.\n";
}

}  // namespace cms
