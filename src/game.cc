#include "game.h"
#include <chrono>
#include <cstdlib>
#include <curses.h>
#include <exception>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <time.h>
#include <thread>
#include <vector>

#include "codes.h"
#include "player.h"
#include "units.h"
#include "utils.h"
#include "ki.h"

#define HELP "soldier [a], gatherer [r], tech [t] | barracks [A], resources [R], defence [D] tech [T] | pause [space] quit [q]"
#define COLOR_DEFAULT 3
#define COLOR_ERROR 2 
#define COLOR_MSG 4

#define LINE_HELP 8
#define LINE_STATUS LINES-9
#define LINE_MSG LINES-8

#define RESOURCES_UPDATE_FREQUENCY 750
#define UPDATE_FREQUENCY 200 

#define KI_NEW_SOLDIER 1250
#define KI_NEW_SOLDIER 1250

std::map<char, int> input_unit_mapping {{'b', Units::BRONZE_GATHERER}, 
  {'s', Units::SILVER_GATHERER}, {'g', Units::GOLD_GATHERER}};

std::string CheckMissingResources(Costs missing_costs) {
  std::string res = "";
  if (missing_costs.gold_ > 0) 
    res += "Missing " + std::to_string(missing_costs.gold_) + " gold! ";
  if (missing_costs.silver_> 0) 
    res += "Missing " + std::to_string(missing_costs.silver_) + " silver! ";
  if (missing_costs.bronze_ > 0) 
    res += "Missing " + std::to_string(missing_costs.bronze_) + " bronze! ";
  if (missing_costs.space_> 0) 
    res += "Missing " + std::to_string(missing_costs.space_) + " space! ";
  return res;
}

Game::Game(int lines, int cols) : game_over_(false), pause_(false) {
  field_ = new Field(lines, cols);
  field_->AddHills();
  player_one_ = new Player(field_->AddDen(lines/1.5, lines-5, cols/1.5, cols-5), 4);
  player_two_ = new Player(field_->AddDen(5, lines/3, 5, cols/3), 100000);
  field_->BuildGraph(player_one_->den_pos(), player_two_->den_pos());
}

void Game::play() {
  std::thread thread_actions([this]() { DoActions(); });
  std::thread thread_choices([this]() { (GetPlayerChoice()); });
  // std::thread thread_ki([this]() { (HandleKi()); });
  thread_actions.join();
  thread_choices.join();
  // thread_ki.join();
}

void Game::DoActions() {
  auto last_update = std::chrono::steady_clock::now();
  auto last_resource_inc = std::chrono::steady_clock::now();
 
  while (!game_over_) {
    auto cur_time = std::chrono::steady_clock::now();

    if (pause_) continue;
    
    // Increase resources.
    if (utils::get_elapsed(last_resource_inc, cur_time) > RESOURCES_UPDATE_FREQUENCY) {
      // Increase players resources.
      player_one_->IncreaseResources();
      last_resource_inc = cur_time;
    }

    if (utils::get_elapsed(last_update, cur_time) > UPDATE_FREQUENCY) {
      // Move player soldiers and check if enemy den's lp is down to 0.
      int damage = player_one_->MoveSoldiers(player_two_->den_pos());
      if (player_two_->DecreaseDenLp(damage)) {
        SetGameOver("YOU WON");
        break;
      }
      damage = player_two_->MoveSoldiers(player_one_->den_pos());
      if (player_one_->DecreaseDenLp(damage)) {
        SetGameOver("YOU LOST");
        break;
      }

      // Remove enemy soldiers in renage of defence towers.
      player_one_->HandleDef(player_two_);
      player_two_->HandleDef(player_one_);

      // Refresh page
      PrintFieldAndStatus();
      last_update = cur_time;
    }
  } 
}

void Game::HandleKi() {
  Ki* ki = new Ki();
  while(!game_over_) {
    if (pause_) continue;

    auto cur_time = std::chrono::steady_clock::now();
    if (utils::get_elapsed(ki->last_soldier(), cur_time) > ki->new_soldier_frequency()) {
      auto pos = field_->GetNewSoldierPos(player_two_->den_pos());
      auto way = field_->GetWayForSoldier(pos, player_one_->den_pos());
      player_two_->AddSoldier(pos, way);
      ki->reset_last_soldier();
    }

    if (utils::get_elapsed(ki->last_def(), cur_time) > ki->new_tower_frequency()) {
      auto pos = field_->FindFree(player_two_->den_pos().first, player_two_->den_pos().second, 1, 5);
      field_->AddNewUnitToPos(pos, Units::DEFENCE);
      player_two_->AddDefenceTower(pos);
      ki->reset_last_def();
    }

    if (utils::get_elapsed(ki->last_update(), cur_time) > ki->update_frequency()) {
      ki->update_frequencies();
      ki->reset_last_update();
    }
  }
}

void Game::GetPlayerChoice() {
  int choice;
  PrintFieldAndStatus();
  while (true) {
    choice = getch();
    // q: quit game
    if (choice == 'q') {
      refresh();
      endwin();
      exit(0);
    }

    // Stop any other player-inputs, if game is over.
    else if (game_over_) {
      continue;
    }

    // SPACE: pause/ unpause game
    else if (choice == ' ') {
      if (pause_) {
        pause_ = false;
        PrintMessage("Un-Paused game.", false);
      }
      else {
        pause_ = true;
        PrintMessage("Paused game.", false);
      }
    }

    // r: add gatherer 
    else if (choice == 'r') {
      PrintMessage("gold-, bronze- or silver-gatherer ([g]/[b]/[s]", false);
      // Select bronze, silver or gold gatherer.
      int add_choice = getch();
      std::string res = "";
      if (input_unit_mapping.count(add_choice) == 0)
        PrintMessage("Invalid choice.", true);
      else {
        int unit = input_unit_mapping.at(add_choice);
        std::string res = CheckMissingResources(player_one_->CheckResources(unit));
        if (res == "") 
          player_one_->AddGatherer(unit);
        PrintMessage(res, res!="");
      }
    }
    // a: add soldier
    else if (choice == 'a') {
      std::string res = CheckMissingResources(player_one_->CheckResources(Units::ATK));
      if (res != "")
        PrintMessage(res, true);
      else {
        auto pos = SelectBarack(player_one_);
        if (pos.first == -1)
          PrintMessage("Invalid choice!", true);
        else {
          PrintMessage("Added soldier at barrack @" + utils::PositionToString(pos), false);
          std::list<Position> way = field_->GetWayForSoldier(pos, player_two_->den_pos());
          player_one_->AddSoldier(pos, way);
        }
      }
    }

    // A: barracks
    else if (choice == 'A') {
      std::string res = CheckMissingResources(player_one_->CheckResources(Units::BARACKS));
      PrintMessage("User the error keys to select a position. Press Enter to select.", false);
      if (res != "") 
        PrintMessage(res, true);
      else {
        Position pos = SelectPosition(player_one_->den_pos(), player_one_->cur_range());
        if (pos.first != -1) {
          player_one_->AddBarrack(pos);
          field_->AddNewUnitToPos(pos, Units::BARACKS);
        }
        PrintMessage(res, res!="");
      }
    }

    // D: place defence-tower
    else if (choice == 'D') {
      std::string res = CheckMissingResources(player_one_->CheckResources(Units::DEFENCE));
      PrintMessage("User the error keys to select a position. Press Enter to select.", false);
      if (res != "") 
        PrintMessage(res, true);
      else {
        Position pos = SelectPosition(player_one_->den_pos(), player_one_->cur_range());
        if (pos.first != -1) {
          player_one_->AddDefenceTower(pos);
          field_->AddNewUnitToPos(pos, Units::DEFENCE);
        }
        PrintMessage(res, res!="");
      }
    }
  } 
}

Position Game::SelectPosition(Position start, int range) {
  bool end = false;
  int choice;
  Position new_pos = {0, 0};
  field_->set_highlight({start});
  field_->set_range(range);
  
  while(!game_over_ && !end) {
    choice = getch();
    new_pos = field_->highlight().front();

    switch (choice) {
      case 10:
        if (field_->IsFree(new_pos))
          end = true;
        else 
          PrintMessage("Invalid position (not free)!", false); 
        break;
      case 'q':
        end = true;
        break;
      case KEY_UP:
        new_pos.first--; 
        PrintFieldAndStatus();
        break;
      case KEY_DOWN:
        new_pos.first++; 
        PrintFieldAndStatus();
        break;
      case KEY_LEFT:
        new_pos.second--;
        PrintFieldAndStatus();
        break;
      case KEY_RIGHT:
        new_pos.second++;
        break;
    }
    // Update highlight only if in range.
    if (field_->InRange(new_pos, range, player_one_->den_pos())) {
      field_->set_highlight({new_pos});
      PrintFieldAndStatus();
    }
    else 
      PrintMessage("Outside of range!", false); 
  }
  field_->set_range(ViewRange::HIDE);
  field_->set_highlight({});
  return new_pos;
}

Position Game::SelectBarack(Player* p) {
  // create replacements (map barack position to letter a..z).
  std::map<Position, char> replacements;
  int counter = 0;
  for (auto it : p->barracks()) 
    replacements[it.first] = (int)'a'+(counter++);
  field_->set_replace(replacements);

  // Print field and get player choice and reset replacements.
  PrintFieldAndStatus();
  PrintMessage("Choose barack: ", false);
  char choice = getch();
  field_->set_replace({});

  // Find selected barack in replacements and return.
  for (auto it : replacements) {
    if (it.second == choice)
      return it.first;
  }
  return {-1, -1};
}

void Game::PrintMessage(std::string msg, bool error) {
  if (error)
    attron(COLOR_PAIR(COLOR_ERROR));
  else
    attron(COLOR_PAIR(COLOR_MSG));
  msg.insert(msg.length(), field_->cols()*2-msg.length(), char(46));
  std::unique_lock ul(mutex_print_field_);
  mvaddstr(LINE_MSG, 10, msg.c_str());
  attron(COLOR_PAIR(COLOR_DEFAULT));
  refresh();
}

void Game::PrintFieldAndStatus() {
  std::unique_lock ul(mutex_print_field_);
  mvaddstr(LINE_HELP, 10, HELP);
  field_->PrintField(player_one_, player_two_);
  mvaddstr(LINE_STATUS, 10, player_one_->GetCurrentStatusLine().c_str());
  refresh();
}

void Game::SetGameOver(std::string msg) {
  clear();
  mvaddstr(LINES/2, COLS/2-4, msg.c_str());
  refresh();
  game_over_ = true;
}
