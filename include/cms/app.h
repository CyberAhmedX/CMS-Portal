#pragma once

#include <string>
#include <vector>

#include "cms/portal.h"

namespace cms {

// What a signed-in user sees. Each role has its own dashboard with its own
// options; the shared loop lives in run().
class Dashboard {
public:
    Dashboard(Portal& portal, const User& user) : portal_(portal), user_(user) {}
    virtual ~Dashboard() = default;

    void run();

protected:
    virtual std::string title() const = 0;
    virtual std::vector<std::string> options() const = 0;
    // Handles one choice. Returns false when the user logs out.
    virtual bool handle(int choice) = 0;

    void changePassword();
    // Lets the user pick a subject from a numbered list. Empty if cancelled.
    std::string pickSubject(const std::vector<Subject>& subjects, const std::string& prompt);

    Portal& portal_;
    const User& user_;
};

class AdminDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::string title() const override;
    std::vector<std::string> options() const override;
    bool handle(int choice) override;

private:
    void findAccount(Role role);
    void listAccounts();
    void resetPassword();
};

class FacultyDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::string title() const override;
    std::vector<std::string> options() const override;
    bool handle(int choice) override;

private:
    void addSubject();
    void listSubjects();
    void markAttendance();
    void giveMarks();
    void classReport();
};

class StudentDashboard final : public Dashboard {
public:
    using Dashboard::Dashboard;

protected:
    std::string title() const override;
    std::vector<std::string> options() const override;
    bool handle(int choice) override;

private:
    void registerSubject();
    void listSubjects();
    void viewMarks();
    void viewAttendance();
};

// The home screen, the three portal screens and first-run setup.
class App {
public:
    explicit App(Portal& portal) : portal_(portal) {}
    int run();

private:
    void setup();
    void portalScreen(Role role);
    void login(Role role);
    void registerAccount(Role role);
    void forgotPassword();

    Portal& portal_;
};

}  // namespace cms
