#include "cms/app.h"

#include <iostream>
#include <map>

#include "cms/console.h"

namespace cms {

namespace c = console;

namespace {

std::string pad(const std::string& text, std::size_t width) {
    return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

std::string portalTitle(Role role) {
    switch (role) {
        case Role::Admin: return "ADMIN PORTAL";
        case Role::Faculty: return "FACULTY PORTAL";
        case Role::Student: return "STUDENT PORTAL";
    }
    return "PORTAL";
}

std::string passwordRules() {
    return "Password: at least " + std::to_string(Portal::kMinPassword) +
           " characters, and not the same as the username.";
}

std::string percent(int part, int whole) {
    if (whole == 0) return "-";
    return std::to_string((part * 100 + whole / 2) / whole) + "%";
}

}  // namespace

// ---------------------------------------------------------------- dashboard

void Dashboard::run() {
    while (true) {
        c::clear();
        c::title(title());
        c::info("Signed in as " + user_.username() + " (" + roleName(user_.role()) + ")");
        std::cout << "\n";
        int choice = c::menu(options());
        bool stay = true;
        try {
            stay = handle(choice);
        } catch (const PortalError& e) {
            c::error(e.what());
        }
        if (!stay) return;
        c::pause();
    }
}

void Dashboard::changePassword() {
    c::heading("Change password");
    std::string current = c::readPassword("Current password: ");
    c::info(passwordRules());
    std::string next = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != next) throw PortalError("The new passwords don't match.");
    portal_.changePassword(user_, current, next);
    c::success("Password changed.");
}

std::string Dashboard::pickSubject(const std::vector<Subject>& subjects, const std::string& prompt) {
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        std::cout << "  " << pad(std::to_string(i + 1) + ".", 4) << subjects[i].name << "\n";
    }
    std::cout << "\n";
    int choice = c::readInt(prompt + " (0 to cancel): ", 0, static_cast<int>(subjects.size()));
    return choice == 0 ? "" : subjects[choice - 1].name;
}

// ---------------------------------------------------------------- admin

std::string AdminDashboard::title() const { return "ADMIN PORTAL"; }

std::vector<std::string> AdminDashboard::options() const {
    return {"Find a faculty member", "Find a student", "List all accounts",
            "Reset a password",      "Change my password", "Log out"};
}

bool AdminDashboard::handle(int choice) {
    switch (choice) {
        case 1: findAccount(Role::Faculty); break;
        case 2: findAccount(Role::Student); break;
        case 3: listAccounts(); break;
        case 4: resetPassword(); break;
        case 5: changePassword(); break;
        default: return false;
    }
    return true;
}

void AdminDashboard::findAccount(Role role) {
    c::heading(role == Role::Faculty ? "Find a faculty member" : "Find a student");
    std::string name = c::readLine("Username: ");
    const User* user = portal_.findUser(role, name);
    if (!user) {
        c::error("No " + roleName(role) + " account named \"" + name + "\".");
        return;
    }
    c::success(user->username() + " is a registered " + roleName(role) + ".");

    if (role == Role::Faculty) {
        std::vector<Subject> taught = portal_.subjectsTaughtBy(*user);
        std::cout << "  Teaches: " << (taught.empty() ? "no subjects yet" : "") << "\n";
        for (const Subject& s : taught) {
            std::cout << "    - " << s.name << " (" << portal_.studentsIn(s.name).size() << " students)\n";
        }
    } else {
        std::vector<std::string> subjects = portal_.enrolledSubjects(user->username());
        std::cout << "  Registered for: " << (subjects.empty() ? "no subjects yet" : "") << "\n";
        for (const std::string& s : subjects) std::cout << "    - " << s << "\n";
    }
}

void AdminDashboard::listAccounts() {
    const std::pair<Role, const char*> groups[] = {
        {Role::Admin, "Admins"}, {Role::Faculty, "Faculty"}, {Role::Student, "Students"}};
    for (const auto& [role, label] : groups) {
        std::vector<const User*> list = portal_.users(role);
        c::heading(std::string(label) + " (" + std::to_string(list.size()) + ")");
        if (list.empty()) std::cout << "  none\n";
        for (const User* u : list) std::cout << "  " << u->username() << "\n";
    }
}

void AdminDashboard::resetPassword() {
    c::heading("Reset a password");
    c::info("Use this when someone forgets their password. It also unlocks the account.");
    std::string name = c::readLine("Username: ");
    c::info(passwordRules());
    std::string password = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != password) throw PortalError("The passwords don't match.");
    portal_.resetPassword(user_, name, password);
    c::success("Password reset. Give the new password to " + Portal::normalizeUsername(name) +
               " privately and ask them to change it.");
}

// ---------------------------------------------------------------- faculty

std::string FacultyDashboard::title() const { return "FACULTY PORTAL"; }

std::vector<std::string> FacultyDashboard::options() const {
    return {"Add a subject", "My subjects",  "Mark attendance",     "Give marks",
            "Class report",  "Change my password", "Log out"};
}

bool FacultyDashboard::handle(int choice) {
    switch (choice) {
        case 1: addSubject(); break;
        case 2: listSubjects(); break;
        case 3: markAttendance(); break;
        case 4: giveMarks(); break;
        case 5: classReport(); break;
        case 6: changePassword(); break;
        default: return false;
    }
    return true;
}

void FacultyDashboard::addSubject() {
    c::heading("Add a subject");
    std::string name = c::readLine("Subject name: ");
    std::string clean = Portal::cleanSubjectName(name);
    int credits = c::readInt("Credit hours (" + std::to_string(Portal::kMinCredits) + "-" +
                                 std::to_string(Portal::kMaxCredits) + "): ",
                             Portal::kMinCredits, Portal::kMaxCredits);
    portal_.addSubject(user_, clean, credits);
    c::success("Added " + clean + ". Students can now register for it.");
}

void FacultyDashboard::listSubjects() {
    c::heading("My subjects");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) {
        std::cout << "  You haven't added any subjects yet.\n";
        return;
    }
    for (const Subject& s : subjects) {
        std::cout << "  " << pad(s.name, 28) << s.creditHours << " credit hours, "
                  << portal_.studentsIn(s.name).size() << " students\n";
    }
}

void FacultyDashboard::markAttendance() {
    c::heading("Mark attendance");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects, "Subject");
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

    std::cout << "\n  Type P for present or A for absent.\n";
    std::map<std::string, bool> present;
    for (const std::string& student : students) {
        while (true) {
            std::string answer = c::readLine(pad(student, 20) + " P/A: ");
            if (answer == "P" || answer == "p") { present[student] = true; break; }
            if (answer == "A" || answer == "a") { present[student] = false; break; }
            c::error("Type P or A.");
        }
    }
    portal_.recordAttendance(user_, subject, date, present);

    int here = 0;
    for (const auto& [_, p] : present) here += p ? 1 : 0;
    c::success("Saved " + subject + " on " + date + ": " + std::to_string(here) + " of " +
               std::to_string(present.size()) + " present.");
}

void FacultyDashboard::giveMarks() {
    c::heading("Give marks");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects, "Subject");
    if (subject.empty()) return;

    std::vector<std::string> students = portal_.studentsIn(subject);
    if (students.empty()) throw PortalError("No students are registered for " + subject + " yet.");

    std::map<std::string, int> current;
    for (const Mark& m : portal_.marksIn(subject)) current[m.student] = m.mark;
    std::cout << "\n";
    for (std::size_t i = 0; i < students.size(); ++i) {
        auto it = current.find(students[i]);
        std::cout << "  " << pad(std::to_string(i + 1) + ".", 4) << pad(students[i], 20)
                  << (it == current.end() ? "no mark yet" : std::to_string(it->second) + "/100") << "\n";
    }
    std::cout << "\n";
    int choice = c::readInt("Student (0 to cancel): ", 0, static_cast<int>(students.size()));
    if (choice == 0) return;

    const std::string& student = students[choice - 1];
    int mark = c::readInt("Mark for " + student + " (" + std::to_string(Portal::kMinMark) + "-" +
                              std::to_string(Portal::kMaxMark) + "): ",
                          Portal::kMinMark, Portal::kMaxMark);
    portal_.setMark(user_, subject, student, mark);
    c::success("Saved: " + student + " got " + std::to_string(mark) + "/100 in " + subject + ".");
}

void FacultyDashboard::classReport() {
    c::heading("Class report");
    std::vector<Subject> subjects = portal_.subjectsTaughtBy(user_);
    if (subjects.empty()) throw PortalError("Add a subject first.");
    std::string subject = pickSubject(subjects, "Subject");
    if (subject.empty()) return;

    std::vector<std::string> students = portal_.studentsIn(subject);
    if (students.empty()) {
        std::cout << "  No students are registered for " << subject << " yet.\n";
        return;
    }
    std::map<std::string, int> marks;
    for (const Mark& m : portal_.marksIn(subject)) marks[m.student] = m.mark;
    std::map<std::string, std::pair<int, int>> attendance;
    for (const AttendanceRecord& r : portal_.attendanceRecords(subject)) {
        auto& [here, total] = attendance[r.student];
        ++total;
        if (r.present) ++here;
    }

    std::cout << "\n  " << pad("Student", 20) << pad("Attendance", 20) << "Mark\n";
    for (const std::string& s : students) {
        auto [here, total] = attendance[s];
        std::string att = total ? std::to_string(here) + "/" + std::to_string(total) + " (" + percent(here, total) + ")" : "-";
        auto it = marks.find(s);
        std::cout << "  " << pad(s, 20) << pad(att, 20) << (it == marks.end() ? "-" : std::to_string(it->second)) << "\n";
    }
}

// ---------------------------------------------------------------- student

std::string StudentDashboard::title() const { return "STUDENT PORTAL"; }

std::vector<std::string> StudentDashboard::options() const {
    return {"Register for a subject", "My subjects", "My marks", "My attendance", "Change my password", "Log out"};
}

bool StudentDashboard::handle(int choice) {
    switch (choice) {
        case 1: registerSubject(); break;
        case 2: listSubjects(); break;
        case 3: viewMarks(); break;
        case 4: viewAttendance(); break;
        case 5: changePassword(); break;
        default: return false;
    }
    return true;
}

void StudentDashboard::registerSubject() {
    c::heading("Register for a subject");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    std::vector<Subject> open;
    for (const Subject& s : portal_.subjects()) {
        bool taken = false;
        for (const std::string& m : mine) taken = taken || m == s.name;
        if (!taken) open.push_back(s);
    }
    if (open.empty()) {
        std::cout << "  There are no new subjects to register for.\n";
        return;
    }
    for (std::size_t i = 0; i < open.size(); ++i) {
        std::cout << "  " << pad(std::to_string(i + 1) + ".", 4) << pad(open[i].name, 28)
                  << open[i].creditHours << " credit hours, taught by " << open[i].faculty << "\n";
    }
    std::cout << "\n";
    int choice = c::readInt("Subject (0 to cancel): ", 0, static_cast<int>(open.size()));
    if (choice == 0) return;
    portal_.enroll(user_, open[choice - 1].name);
    c::success("You are registered for " + open[choice - 1].name + ".");
}

void StudentDashboard::listSubjects() {
    c::heading("My subjects");
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    if (mine.empty()) {
        std::cout << "  You haven't registered for any subjects yet.\n";
        return;
    }
    int credits = 0;
    for (const std::string& name : mine) {
        std::optional<Subject> s = portal_.findSubject(name);
        int hours = s ? s->creditHours : 0;
        credits += hours;
        std::cout << "  " << pad(name, 28) << hours << " credit hours, taught by " << (s ? s->faculty : "-") << "\n";
    }
    std::cout << "\n  Total: " << credits << " credit hours\n";
}

void StudentDashboard::viewMarks() {
    c::heading("My marks");
    std::map<std::string, int> marks;
    for (const Mark& m : portal_.marksFor(user_.username())) marks[m.subject] = m.mark;
    std::vector<std::string> mine = portal_.enrolledSubjects(user_.username());
    if (mine.empty()) {
        std::cout << "  You haven't registered for any subjects yet.\n";
        return;
    }
    for (const std::string& subject : mine) {
        auto it = marks.find(subject);
        std::cout << "  " << pad(subject, 28) << (it == marks.end() ? "not marked yet" : std::to_string(it->second) + "/100") << "\n";
    }
}

void StudentDashboard::viewAttendance() {
    c::heading("My attendance");
    std::vector<AttendanceSummary> list = portal_.attendanceFor(user_.username());
    if (list.empty()) {
        std::cout << "  You haven't registered for any subjects yet.\n";
        return;
    }
    for (const AttendanceSummary& a : list) {
        std::cout << "  " << pad(a.subject, 28);
        if (a.total == 0) {
            std::cout << "no classes yet\n";
        } else {
            std::cout << a.attended << " of " << a.total << " classes (" << percent(a.attended, a.total) << ")\n";
        }
    }
}

// ---------------------------------------------------------------- app

int App::run() {
    try {
        if (portal_.needsSetup()) setup();
        while (true) {
            c::clear();
            c::title("CMS PORTAL");
            int choice = c::menu({"Admin portal", "Faculty portal", "Student portal", "Exit"});
            if (choice == 4) break;
            portalScreen(choice == 1 ? Role::Admin : choice == 2 ? Role::Faculty : Role::Student);
        }
    } catch (const c::InputClosed&) {
        std::cout << "\n";
    }
    std::cout << "\n  Goodbye.\n";
    return 0;
}

void App::setup() {
    c::clear();
    c::title("FIRST RUN SETUP");
    c::box({"There is no admin account yet.", "Create one now. It is the only account that",
            "can look up users and reset passwords."});
    std::cout << "\n";
    while (portal_.needsSetup()) {
        try {
            std::string name = c::readLine("Admin username: ");
            c::info(passwordRules());
            std::string password = c::readPassword("Admin password: ");
            if (c::readPassword("Repeat password: ") != password) throw PortalError("The passwords don't match.");
            portal_.createFirstAdmin(name, password);
            c::success("Admin account created.");
            c::pause();
        } catch (const PortalError& e) {
            c::error(e.what());
        }
    }
}

void App::portalScreen(Role role) {
    while (true) {
        c::clear();
        c::title(portalTitle(role));
        // Admin accounts can't be self-registered, so admins get no Register option.
        static const std::vector<std::string> adminOptions = {"Log in", "Forgot password", "Back"};
        static const std::vector<std::string> userOptions = {"Log in", "Register", "Forgot password", "Back"};
        const std::vector<std::string>& options = role == Role::Admin ? adminOptions : userOptions;
        const std::string& picked = options[c::menu(options) - 1];

        if (picked == "Back") return;
        try {
            if (picked == "Log in") {
                login(role);
                continue;  // the dashboard already paused where needed
            }
            if (picked == "Register") registerAccount(role);
            if (picked == "Forgot password") forgotPassword();
        } catch (const PortalError& e) {
            c::error(e.what());
        }
        c::pause();
    }
}

void App::login(Role role) {
    c::heading("Log in");
    std::string name = c::readLine("Username: ");
    std::string password = c::readPassword("Password: ");
    LoginResult result = portal_.login(role, name, password);

    if (result.status == LoginStatus::Locked) {
        std::int64_t minutes = (result.secondsLocked + 59) / 60;
        c::error("Too many wrong passwords. Try again in " + std::to_string(minutes) +
                 (minutes == 1 ? " minute." : " minutes."));
        c::pause();
        return;
    }
    if (result.status != LoginStatus::Ok) {
        c::error("Wrong username or password.");
        c::pause();
        return;
    }

    switch (role) {
        case Role::Admin: AdminDashboard(portal_, *result.user).run(); break;
        case Role::Faculty: FacultyDashboard(portal_, *result.user).run(); break;
        case Role::Student: StudentDashboard(portal_, *result.user).run(); break;
    }
}

void App::registerAccount(Role role) {
    c::heading("Register");
    c::info("Username: 3 to 32 letters, digits, '.', '_' or '-'.");
    std::string name = c::readLine("Username: ");
    Portal::checkUsername(name);
    c::info(passwordRules());
    std::string password = c::readPassword("Password: ");
    if (c::readPassword("Repeat password: ") != password) throw PortalError("The passwords don't match.");
    portal_.registerUser(role, name, password);
    c::success("Account created. You can log in now.");
}

void App::forgotPassword() {
    c::heading("Forgot password");
    c::box({"Passwords are stored as salted one-way hashes, so",
            "nobody can look yours up, not even the admin.", "",
            "Ask the admin to reset it. They will give you a",
            "new password, which you can change after logging in."});
}

}  // namespace cms
