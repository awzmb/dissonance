#include <bits/types/FILE.h>
#include <cctype>
#include <cmath>
#include <curses.h>
#include <exception>
#include <iostream>
#include <locale>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>
#include "field.h"
#include "player.h"
#include "units.h"
#include "utils.h"
#include "codes.h"

#define COLOR_DEFAULT 3
#define COLOR_PLAYER 2 
#define COLOR_KI 1 
#define COLOR_RESOURCES 4 
#define COLOR_OK 5 
#define COLOR_HIGHLIGHT 6 

#define HILL ' ' 
#define DEN 'D' 
#define GOLD 'G' 
#define SILVER 'S' 
#define BRONZE 'B' 
#define FREE char(46)
#define DEF 'T'
#define BARACK 'A'

int getrandom_int(int min, int max);
int random_coordinate_shift(int x, int min, int max);

Field::Field(int lines, int cols) {
  lines_ = lines;
  cols_ = cols;
  printw("Using lines: %u, using cols: %u. \n", lines_, cols_);

  // initialize empty field.
  for (int l=0; l<=lines_; l++) {
    field_.push_back({});
    for (int c=0; c<=cols_; c++)
      field_[l].push_back(char(46));
  }

  highlight_ = {};
  range_ = ViewRange::HIDE;
}

// getter
int Field::lines() { 
  return lines_; 
}
int Field::cols() { 
  return cols_; 
}
std::vector<Position> Field::highlight() {
  return highlight_;
}

// setter:
void Field::set_highlight(std::vector<Position> positions) {
  highlight_ = positions;
}
void Field::set_range(int range) {
  range_ = range;
}
void Field::set_replace(std::map<Position, char> replacements) {
  replacements_ = replacements;
}

Position Field::AddDen(int min_line, int max_line, int min_col, int max_col) {
  Position pos = {getrandom_int(min_line, max_line), getrandom_int(min_col, cols_-5)};
  field_[pos.first][pos.second] = DEN;
  ClearField(pos);
  AddResources(pos);
  return pos;
}

void Field::ClearField(Position pos) {
  field_[pos.first][pos.second+1] = FREE;
  field_[pos.first+1][pos.second] = FREE;
  field_[pos.first][pos.second-1] = FREE;
  field_[pos.first-1][pos.second] = FREE;
  field_[pos.first+1][pos.second+1] = FREE;
  field_[pos.first+1][pos.second-1] = FREE;
  field_[pos.first-1][pos.second+1] = FREE;
  field_[pos.first-1][pos.second-1] = FREE;
}

void Field::AddResources(Position start_pos) {
  auto pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = GOLD;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = SILVER;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = BRONZE;
}

void Field::BuildGraph(Position player_den, Position enemy_den) {
  // Add all nodes.
  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      if (field_[l][c] != HILL)
        graph_.AddNode(l, c);
    }
  }

  // For each node, add edges.
  for (auto node : graph_.nodes()) {
    // Node above 
    Position pos = {node.second->line_-1, node.second->col_};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // Node below
    pos = {node.second->line_+1, node.second->col_};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // Node left
    pos = {node.second->line_, node.second->col_-1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // Node right 
    pos = {node.second->line_, node.second->col_+1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // Upper left
    pos = {node.second->line_-1, node.second->col_-1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // lower left 
    pos = {node.second->line_+1, node.second->col_-1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // Upper right
    pos = {node.second->line_-1, node.second->col_+1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    // lower right 
    pos = {node.second->line_+1, node.second->col_+1};
    if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != HILL && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
  }

  // Remove all nodes not in main circle
  graph_.RemoveInvalid(player_den);
  if (graph_.nodes().count(enemy_den) == 0)
    throw "Invalid world.";
}

void Field::AddHills() {
  int num_hils = (lines_ + cols_) * 2;
  // Generate lines*2 mountains.
  for (int i=0; i<num_hils; i++) {
    // Generate random hill.
    int start_y = getrandom_int(0, lines_);
    int start_x = getrandom_int(0, cols_);
    field_[start_y][start_x] = HILL;

    // Generate random 5 hills around this hill.
    for (int j=1; j<=5; j++) {
      int y = GetXInRange(random_coordinate_shift(start_y, 0, j), 0, lines_);
      int x = GetXInRange(random_coordinate_shift(start_x, 0, j), 0, cols_);
      field_[y][x] = HILL;
    }
  }
}

Position Field::GetNewSoldierPos(Position pos) {
  auto new_pos = FindFree(pos.first, pos.second, 1, 3);
  while(!graph_.InGraph(new_pos))
    new_pos = FindFree(pos.first, pos.second, 1, 3);
  return new_pos;
}

std::list<Position> Field::GetWayForSoldier(Position start_pos, Position target_pos) {
  return graph_.find_way(start_pos, target_pos);
}

void Field::AddNewUnitToPos(Position pos, int unit) {
  std::unique_lock ul_field(mutex_field_);
  if (unit == Units::DEFENCE)
    field_[pos.first][pos.second] = DEF;
  else if (unit == Units::BARACKS)
    field_[pos.first][pos.second] = BARACK;
}

void Field::UpdateField(Player *player, std::vector<std::vector<char>>& field) {
  for (auto it : player->soldier()) { // player-soldier does not need to be locked, as copy is returned
    int l = it.second.pos_.first;
    int c = it.second.pos_.second;
    if (field[l][c] == FREE)
      field[l][c] = '1';
    else {
      std::locale loc;
      char val = field[l][c];
      if (std::isdigit(val, loc)) {
        int num = val - '0';
        field[l][c] = num+1 + '0';
      }
    }
  }
}

void Field::PrintField(Player* player, Player* enemy) {
  std::shared_lock sl_field(mutex_field_);
  auto field = field_;
  sl_field.unlock();

  UpdateField(player, field);
  UpdateField(enemy, field);

  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      Position cur = {l, c};
      // highlight -> magenta
      if (std::find(highlight_.begin(), highlight_.end(), cur) != highlight_.end())
        attron(COLOR_PAIR(COLOR_HIGHLIGHT));
      // both players -> cyan
      else if (enemy->IsSoldier(cur) && player->IsSoldier(cur))
        attron(COLOR_PAIR(COLOR_RESOURCES));
      // player 2 -> red
      else if (enemy->units_and_buildings().count(cur) > 0 || enemy->IsSoldier(cur))
        attron(COLOR_PAIR(COLOR_PLAYER));
      // player 1 -> blue 
      else if (player->units_and_buildings().count(cur) > 0 || player->IsSoldier(cur))
        attron(COLOR_PAIR(COLOR_KI));
      // resources -> cyan
      else if (field[l][c] == 'G' || field[l][c] == 'S' ||field[l][c] == 'B')
         attron(COLOR_PAIR(COLOR_RESOURCES));
      // range -> green
      else if (InRange(cur, range_, player->den_pos()))
        attron(COLOR_PAIR(COLOR_OK));
      
      // Replace certain elements.
      if (replacements_.count(cur) > 0)
        mvaddch(10+l, 10+2*c, replacements_.at(cur));
      else
        mvaddch(10+l, 10+2*c, field[l][c]);
      mvaddch(10+l, 10+2*c+1, ' ' );
      attron(COLOR_PAIR(COLOR_DEFAULT));
    }
  }
}

bool Field::InRange(Position pos, int range, Position start) {
  if (range == ViewRange::GRAPH)
    return graph_.InGraph(pos);
  return utils::dist(pos, start) <= range;
}

Position Field::GetSelected(char replace, int num) {
  int counter = int('a')-1;
  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      if (field_[l][c] == replace)
        counter++;
      if (counter == num)
        return {l, c};
    }
  }
  return {-1, -1};
}

bool Field::InField(int l, int c) {
  return (l >= 0 && l <= lines_ && c >= 0 && c <= cols_);
}

Position Field::FindFree(int l, int c, int min, int max) {
  std::shared_lock sl_field(mutex_field_);
  for (int attempts=0; attempts<100; attempts++) {
    for (int i=0; i<max; i++) {
      int y = GetXInRange(random_coordinate_shift(l, min, min+i), 0, lines_);
      int x = GetXInRange(random_coordinate_shift(c, min, min+i), 0, cols_);
      if (field_[y][x] == FREE)
        return {y, x};
    }
  }    
  exit(400);
}

bool Field::IsFree(Position pos) {
  return field_[pos.first][pos.second] == FREE;
}

int Field::GetXInRange(int x, int min, int max) {
  if (x < min) 
    return min;
  if (x > max)
    return max;
  return x;
}

int getrandom_int(int min, int max) {
  int ran = min + (rand() % (max - min + 1)); 
  return ran;
}

int random_coordinate_shift(int x, int min, int max) {
  // Determine decrease or increase of values.
  int plus_minus = getrandom_int(0, 1);
  int random_faktor = getrandom_int(min, max);
  return x + pow(-1, plus_minus)*random_faktor;
}

