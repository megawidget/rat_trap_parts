/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <random>
#include <sstream>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

#include <wn.h> // for in_wn and morphword

#include "rat_trap_parts.hpp"
#include "ncurses_wrappers.hpp"

#define HUNSPELL_AFF "en_US.aff"
#define HUNSPELL_DIC "en_US.dic"

#define SCORE_STR "Score:"
#define FINAL_SCORE_STR "Your final score is "
#define PRIOR_WORDS_STR "Prior words:"
#define CURRENT_WORDS_STR "Current words:"
#define PROMPT_STR ">"

const static std::string prior_words_row(std::string(PRIOR_WORDS_STR) +
		std::string(MAX_COLS - strlen(PRIOR_WORDS_STR), ' '));
const static std::string current_words_row(std::string(CURRENT_WORDS_STR) +
		std::string(MAX_COLS - strlen(CURRENT_WORDS_STR), ' '));

bool lowercase_and_validate(std::string& str) {
	to_lower(str);
	return std::all_of(str.begin(), str.end(), isalpha);
}

template<size_t size> void paginate(std::set<word const> const& from,
		std::vector<std::array<std::string, size> >& to) {
	to.clear();
	to.emplace_back();
	std::string row;
	int row_index = 0;
	for (auto const& str : from) {
		if (row.size() + str.literal.size() < MAX_COLS) {
			row += str.literal + " ";
			continue;
		}
		if (row_index == size) {
			to.emplace_back();
			row_index = 0;
		}
		to.back()[row_index++] = row;
		row.clear();
	}
	if (row.size() > 0) {
		if (row_index == size) {
			to.emplace_back();
			row_index = 0;
		}
		to.back()[row_index++] = row;
	}
}

word::word(std::string const& w) : literal(w), sorted(w) {
	std::sort(sorted.begin(), sorted.end());
}

bool word::operator< (word const& other) const {
	return literal < other.literal;
}

bool word::is_one_less_than(std::vector<std::string const>& other) const {
	std::string o = join(other, "");

	// is length mismatched?
	if (o.size() - sorted.size() != 1) {
		return false;
	}

	// ensure we differ by only one letter
	std::sort(o.begin(), o.end());
	int i, j;
	i = j = 0;
	while (i < sorted.size() && j < o.size()) {
		if (sorted[i] == o[j]) {
			i++;
			j++;
			continue;
		}
		if (j - i > 1) return false;
		j++;
	}
	return true;
}

std::set<std::string const> rat_trap_parts::stems_from_str(
		std::string const& str) {
	std::set<std::string const> stems;
	char literal_arr[128];

	if (str.size() >= sizeof(literal_arr)) {
		throw std::runtime_error("Input length exceeded.");
	}

	std::string literal = str;
	if (!lowercase_and_validate(literal) || !spell.spell(str.c_str())) {
		return stems;
	}

	bool should_hunspell = false;

	strcpy(literal_arr, literal.c_str());
	// morph the str to base form first
	for (int i = NOUN; i <= ADV; i++) {
		char* buf = morphword(literal_arr, i);
		// if already base form, be sure to check with hunspell before adding
		if (buf == nullptr) {
			if (in_wn(literal_arr, i)) {
				should_hunspell = true;
			}
			continue;
		}
		stems.emplace(buf);
	}

	// then try stemming it
	if (should_hunspell) {
		char** stems_arr;
		int stems_count = spell.stem(&stems_arr, literal_arr);
		for(int i = 0; i < stems_count; i++) {
			stems.emplace(stems_arr[i]);
			i++;
		}
		if (stems_count > 0) {
			spell.free_list(&stems_arr, stems_count);
		}
	}

	return stems;
};

void rat_trap_parts::adjust_screen_dimensions() {
	int row, col;
	getmaxyx(stdscr, row, col);
	if (ERR == wresize(stdscr, row < MAX_ROWS ? MAX_ROWS : row,
				col < MAX_COLS ? MAX_COLS : col)) {
		throw std::runtime_error(
				"Failed to resize ncurses window to 80x24 or greater.");
	}
};

void rat_trap_parts::help() {
	clear();
	for (int i = 0, j = 0; i < readme_lines.size(); i++, j++) {
		if (i != readme_lines.size() - 1 &&
				readme_lines[i+1].size() == readme_lines[i].size() &&
				std::all_of(readme_lines[i+1].begin(), readme_lines[i+1].end(),
					[] (char c) { return c == '='; })) {
			rmvprintw(j, 0, readme_lines[i].c_str());
			i++;
		} else {
			mvprintw(j, 0, readme_lines[i].c_str());
		}
	}
	print_err("Press any key to return to the game.");
	refresh();
	noecho();
	getch();
	echo();
	clear();
}

void rat_trap_parts::setup() {
	// initialize readme
	char readme[81*40];
	FILE* f = fopen("README.md", "r");
	if (f == nullptr) {
		throw std::runtime_error("Couldn't read README.md.");
	}
	int read = fread(readme, 1, sizeof(readme), f);
	assert(read > 0);
	assert(read < 81*40);
	std::stringstream ss(readme);
	std::string line;
	while(std::getline(ss, line, '\n')) {
		readme_lines.push_back(line);
	}

	while(current.size() == 0) {
		clear();
		mvprintw(3, MAX_COLS/2 - sizeof("welcome to")/2, "welcome to");
		mvprintw(5, MAX_COLS/2 - sizeof("R A T")/2, "R A T");
		mvprintw(6, MAX_COLS/2 - sizeof("T R A P")/2, "T R A P");
		mvprintw(7, MAX_COLS/2 - sizeof("P A R T S")/2, "P A R T S");
		rmvprintw(21, 0, "Enter a 3-letter word to start with.");
		rmvprintw(22, 0, "'r' or 'random' for random start, 'h' for help.");
		rmvprintw(PROMPT_ROW, 0, PROMPT_STR);
		refresh();
		mvgetnstr(PROMPT_ROW, 2, input_arr, sizeof(input_arr));
		std::string str(input_arr);
		if (lowercase_and_validate(str)) {
			if (str.size() == 3 && spell.spell(str.c_str())) {
				current.insert(str);
				std::set<std::string const> stems = stems_from_str(str);
				used_stems.insert(stems.begin(), stems.end());
				return;
			} else if (str == "r" || str == "random") {
				FILE* f = fopen("valid_words.txt", "r");
				if (f == nullptr) {
					throw std::runtime_error("Couldn't read valid words.");
				}
				std::vector<std::string const> choices;
				char data[10240];
				int read = fread(data, 4, sizeof(data)/4, f);
				assert(read > 0);
				fclose(f);
				if (ferror(f) != 0) {
					throw std::runtime_error("Couldn't read valid words.");
				}
				for (char* start = data; start < data + read*4 - 3; start += 4) {
					start[3] = '\0';
					choices.emplace_back(start);
				}
				std::string choice = choices[std::random_device()()%choices.size()];
				current.insert(choice);
				std::set<std::string const> stems = stems_from_str(choice);
				used_stems.insert(stems.begin(), stems.end());
				return;
			} else if (str == "h" || str == "help") {
				help();
			}
		}
	}
}

void rat_trap_parts::play() {
	char line_buffer[MAX_COLS + 1];

	setup();
	clear();

	paginate(prior, prior_strings);
	paginate(current, current_strings);

	print_err("If confused, press h<Enter>");
	while (true) {
		rmvprintw(SCORE_ROW, 0, SCORE_STR);
		rmvprintw(PROMPT_ROW, 0, PROMPT_STR);
		rmvprintw(1, 0, prior_words_row.c_str());
		rmvprintw(17, 0, current_words_row.c_str());
		snprintf(line_buffer, MAX_COLS, " %lu", score);
		mvprintw(SCORE_ROW, sizeof(SCORE_STR), line_buffer);
		if (prior_strings.size() > 0) {
			for (int i = PRIOR_START; i <= PRIOR_END; i++) {
				mvprintw(i, 0,
						prior_strings[prior_index].data()[i - PRIOR_START].c_str());
			}
		}
		assert(current_strings.size() > 0);
		for (int i = CURRENT_START; i <= CURRENT_END; i++) {
			mvprintw(i, 0,
					current_strings[current_index].data()[i - CURRENT_START].c_str());
		}

		refresh();
		mvgetnstr(23, 1, input_arr, sizeof(input_arr));
		clear();
		print_blank();
		std::string input(input_arr);
		for (auto& c : input) {
			c = tolower(c);
		}
		if (input == ",") {
			prior_index = std::max(static_cast<unsigned int>(0), prior_index - 1);
			continue;
		} else if (input == ".") {
			prior_index = std::min(prior_strings.size(),
					static_cast<unsigned long>(prior_index + 1));
			continue;
		} else if (input == "<") {
			current_index = std::max(static_cast<unsigned int>(0),
					current_index - 1);
			continue;
		} else if (input == ">") {
			current_index = std::min(current_strings.size(),
					static_cast<unsigned long>(current_index + 1));
			continue;
		} else if (input == "q") {
			for (auto const& c : current) {
				score += c.literal.size() - 3;
			}
			snprintf(line_buffer, MAX_COLS, "Your final score is %lu", score);
			mvprintw(SCORE_ROW, 0, line_buffer);
			print_err("Press any key to continue...");
			refresh();
			noecho();
			getch();
			echo();
			return;
		} else if (input == "?" || input == "h") {
			help();
			print_blank();
			continue;
		}

		char* start = input_arr;
		char* token;

		// is the first word in our current set?
		token = strsep(&start, " ");
		std::string chosen(token);
		if (current.count(chosen) == 0) {
			print_err("'%s' is not a current word.", chosen.c_str());
			continue;
		}

		// make sure the candidates are are lowercase alpha and at least 3 chars
		// long
		std::vector<std::string const> candidates;
		bool entry_invalid = false;
		if (start == nullptr) {
			print_err("Need at least one word...");
			continue;
		}

		while(start != nullptr) {
			std::string str(strsep(&start, " "));
			if (!lowercase_and_validate(str) || str.size() < 3) {
				print_err("'%s' is not alpha/too short", str.c_str());
				entry_invalid = true;
				break;
			}
			candidates.push_back(str);
		}
		if (entry_invalid) continue;

		if (!word(chosen).is_one_less_than(candidates)) {
			print_err("Not a valid anagram + extra letter");
			continue;
		}

		int score_this_round = 0;
		std::set<std::string const> stems_this_round;
		for (auto const& candidate : candidates) {
			std::set<std::string const> stems = stems_from_str(candidate);
			// is this even a real word?
			if (stems.size() == 0) {
				print_err("'%s' isn't a valid word", candidate.c_str());
				entry_invalid = true;
				break;
			}
			// is at least one stem of this word used?
			bool was_scored = false;
			for (auto const& stem : stems) {
				if (used_stems.count(stem) == 0 && stems_this_round.count(stem) == 0) {
					stems_this_round.insert(stem);
					if (!was_scored) {
						score_this_round += candidate.size() - 3;
						was_scored = true;
					}
				} else {
					print_err("'%s' already used previously", candidate.c_str());
					entry_invalid = true;
					break;
				}
			}
		}
		if (entry_invalid) continue;
		score += score_this_round;
		used_stems.insert(stems_this_round.begin(), stems_this_round.end());
		current.erase(chosen);
		prior.insert(chosen);
		current.insert(candidates.begin(), candidates.end());
		paginate(prior, prior_strings);
		paginate(current, current_strings);
	}
};

rat_trap_parts::rat_trap_parts() : spell(HUNSPELL_AFF, HUNSPELL_DIC),
		prior_index(0), current_index(0), score(0) {
	if (wninit() != 0) {
		throw std::runtime_error("Failed to initialize WordNet.");
	}
	if (initscr() == nullptr) {
		throw std::runtime_error("Failed to initialize ncurses.");
	}
};

rat_trap_parts::~rat_trap_parts() {
	endwin();
};

void rat_trap_parts::go() {
	adjust_screen_dimensions();
	echo();

	play();
};
