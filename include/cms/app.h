#pragma once

#include <string>
#include <vector>

#include "cms/console.h"
#include "cms/portal.h"

namespace cms {

// What a signed-in user sees: the campus card, a row of tabs and the page
// for the active tab. Each role has its own tabs and pages; the shared loop
// lives in run().
class Dashboard {
public:
    Dashboard(Portal& portal, const User& user) : portal_(portal), user_(user) {}
    virtual ~Dashboard() = default;

    void run();

protected:
    virtual std::vector<console::Tab> tabs() const = 0;  // includes {0, "Log out"}
    // Draws the page for a tab. A form page asks its questions right here.
    virtual void show(int tab) = 0;
    // Warnings shown once, right after logging in.
    virtual std::vector<std::string> alerts() const { return {}; }

    void account();
    void changePassword();
    // Lets the user pick a subject from a numbered list. Empty if cancelled.
    std::string pickSubject(const std::vector<Subject>& subjects);

    Portal& portal_;
    const User& user_;
};

class AdminDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::vector<console::Tab> tabs() const override;
    void show(int tab) override;
    std::vector<std::string> alerts() const override;

private:
    void facultyList();
    void studentList();
    void atRisk();
    void findAccount();
    void resetPassword();
    void settings();
};

class FacultyDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::vector<console::Tab> tabs() const override;
    void show(int tab) override;
    std::vector<std::string> alerts() const override;

private:
    void mySubjects();
    void addSubject();
    void markAttendance();
    void giveMarks();
    void classReport();
};

class StudentDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::vector<console::Tab> tabs() const override;
    void show(int tab) override;
    std::vector<std::string> alerts() const override;

private:
    void mySubjects();
    void registerSubject();
    void myMarks();
    void myAttendance();
};

// The welcome screen, the three portal sign-in screens and first-run setup.
class App {
public:
    explicit App(Portal& portal) : portal_(portal) {}
    int run();

private:
    void setup();
    void portalScreen(Role role);
    bool login(Role role);  // true after the user has signed in and out again
    void registerAccount(Role role);
    void forgotPassword();

    Portal& portal_;
};

}  // namespace cms
