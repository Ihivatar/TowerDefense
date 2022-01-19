#include <SFML/Graphics.hpp>

#include <vector>
#include <unordered_map>
#include <iostream>
#include <sstream>

const int WIDTH = 1600;
const int HEIGHT = 900;

// Sizes are in pixels.
const float MONSTER_SIZE = 32.0f;
const float WAYPOINT_RADIUS = 16.0f;
const float TOWER_RADIUS = 16.0f;
const float BULLET_RADIUS = 8.0f;

// Speed is pixels per second.
const float MONSTER_SPEED = 100.0f;
const float BULLET_SPEED = 150.0f;

const uint32_t MONSTER_MAX_HEALTH = 100;

//
// This is a simple Tower Defense style game.
// It is written using the Entity Component System (ECS) style.
// This states that Entities should simply be a unique handle (uint32_t type).
// This handle would index into various Component arrays. Components
// are themselves PoD structs with no logic. The logic is relegated
// to Systems (stand alone functions).
// ECS was chosen to provide fewer cache misses and better
// memory alignment. This allows much more entities on the
// screen at a single time.
// 
// This implementation is not currently following pure ECS style.
// A large speedup could be had if the Entity structs were decomposed
// into multiple Component arrays.
// e.g.
// struct Tower current has: Position, AttackRange, AttackRate, and Timer Components
// that are stored inside the Tower struct.
// End goal would be to not have a Tower struct and just have Component arrays:
// std::vector<Position> tower_position
// std::vector<AttackRange> tower_attack_range
// std::vector<AttackRate> tower_attack_rate
// std::vector<Timer> tower_timer
// Then every time a Tower was "created", new data would be emplaced_back() into
// each of these arrays.
//

//
// Base Components.
//

// Alignment values assume x64.
// 4 byte aligned, 4 byte size.
struct Health
{
	uint32_t value;
};

// 4 byte aligned, 8 byte size.
struct Position
{
	float x;
	float y;
};

// 4 byte aligned, 8 byte size.
struct Velocity
{
	float x;
	float y;
};

// 4 byte aligned, 4 byte size.
struct Damage
{
	uint32_t value;
};

// 4 byte aligned, 4 byte size.
struct AttackRange
{
	float value;
};

// 4 byte aligned, 4 byte size.
// The number of seconds between each shot.
struct AttackRate
{
	float value;
};

// 4 byte aligned, 4 byte size.
struct Timer
{
	float value;
};

//
// Entity types (comprised of base components).
//

// 4 byte aligned, 28 byte size.
struct Monster
{
	Health health;
	Position position;
	Velocity velocity;
	uint32_t waypoint_index;	// Index into waypoints vector, this is the currently targeted waypoint.
	Damage damage;
};

// 4 byte aligned, 8 byte size.
struct Waypoint
{
	Position position;
};

// 4 byte aligned, 20 byte size.
struct Tower
{
	Position position;
	AttackRange range;
	AttackRate attackRate;
	Timer timer;
};

// 4 byte aligned, 16 byte size.
struct Bullet
{
	Position position;
	Velocity velocity;
	Damage damage;
	uint32_t target_index;		// Index into monsters vector, this is the current target.
								// This enables the bullets to track their target and home in.
};

//
// Systems (functions that act on entities and components).
//

float Distance(Position pos1, Position pos2)
{
	return sqrtf((pos2.x - pos1.x) * (pos2.x - pos1.x) + (pos2.y - pos1.y) * (pos2.y - pos1.y));
}

float Magnitude(float x, float y)
{
	return sqrtf(x * x + y * y);
}

sf::Vector2f Normalize(float x, float y)
{
	sf::Vector2f result;
	const float magnitude = Magnitude(x, y);
	result.x = x / magnitude;
	result.y = y / magnitude;

	return result;
}

void DrawMonsters(const std::vector<Monster>& monsters, sf::RenderTarget& target)
{
	sf::RectangleShape shape;
	shape.setFillColor(sf::Color::Red);
	shape.setSize(sf::Vector2f(MONSTER_SIZE, MONSTER_SIZE));
	shape.setOrigin(MONSTER_SIZE / 2.0f, MONSTER_SIZE / 2.0f);	// Set origin (0, 0 coordinate) of shape to the center of the shape instead of top-left corner.

	const float bar_height = 3.0f;

	sf::RectangleShape healthBar;
	healthBar.setFillColor(sf::Color::Red);
	healthBar.setSize(sf::Vector2f(MONSTER_SIZE, bar_height));
	healthBar.setOrigin(MONSTER_SIZE / 2.0f, bar_height / 2.0f);
	healthBar.setOutlineThickness(1.0f);
	healthBar.setOutlineColor(sf::Color::Black);

	sf::RectangleShape health;
	health.setFillColor(sf::Color::Green);
	health.setSize(sf::Vector2f(MONSTER_SIZE, bar_height));
	health.setOrigin(MONSTER_SIZE / 2.0f, bar_height / 2.0f);

	for (uint32_t i = 0; i < monsters.size(); ++i)
	{
		shape.setPosition(monsters[i].position.x, monsters[i].position.y);
		target.draw(shape);

		healthBar.setPosition(monsters[i].position.x, monsters[i].position.y - (MONSTER_SIZE / 2.0f) - 5.0f);
		target.draw(healthBar);

		health.setSize(sf::Vector2f(MONSTER_SIZE * (monsters[i].health.value / (float)MONSTER_MAX_HEALTH), bar_height));
		health.setPosition(monsters[i].position.x, monsters[i].position.y - (MONSTER_SIZE / 2.0f) - 5.0f);
		target.draw(health);
	}
}

void DrawWaypoints(const std::vector<Waypoint>& waypoints, sf::RenderTarget& target)
{
	sf::CircleShape shape;
	shape.setFillColor(sf::Color::Blue);
	shape.setRadius(WAYPOINT_RADIUS);
	shape.setOrigin(WAYPOINT_RADIUS, WAYPOINT_RADIUS); // Set origin to center of shape instead of top-left corner.
	for (uint32_t i = 0; i < waypoints.size(); ++i)
	{
		shape.setPosition(waypoints[i].position.x, waypoints[i].position.y);
		target.draw(shape);
	}
}

void DrawTowers(const std::vector<Tower>& towers, sf::RenderTarget& target)
{
	// Tower.
	sf::CircleShape shape;
	shape.setFillColor(sf::Color::Green);
	shape.setRadius(TOWER_RADIUS);
	shape.setOrigin(TOWER_RADIUS, TOWER_RADIUS); // Set origin to center of shape instead of top-left corner.

	// AttackRange circle.
	sf::CircleShape attackRange;
	attackRange.setFillColor(sf::Color::Transparent);
	attackRange.setOutlineColor(sf::Color::Black);
	attackRange.setOutlineThickness(1.0f);

	for (uint32_t i = 0; i < towers.size(); ++i)
	{
		// Draw tower.
		shape.setPosition(towers[i].position.x, towers[i].position.y);
		target.draw(shape);

		// Draw attack range circle.
		attackRange.setRadius(towers[i].range.value);
		attackRange.setOrigin(towers[i].range.value, towers[i].range.value);
		attackRange.setPosition(towers[i].position.x, towers[i].position.y);
		target.draw(attackRange);
	}
}

void DrawBullets(const std::vector<Bullet>& bullets, sf::RenderTarget& target)
{
	sf::CircleShape shape;
	shape.setFillColor(sf::Color::Cyan);
	shape.setRadius(BULLET_RADIUS);
	shape.setOrigin(BULLET_RADIUS, BULLET_RADIUS); // Set origin to center of shape instead of top-left corner.
	for (uint32_t i = 0; i < bullets.size(); ++i)
	{
		shape.setPosition(bullets[i].position.x, bullets[i].position.y);
		target.draw(shape);
	}
}

// Returns false if Monster is dead.
bool UpdateMonster(Monster& monster, float DeltaTime, const std::vector<Waypoint>& waypoints, uint32_t& player_health)
{
	// Are we dead?
	if (monster.health.value <= 0)
	{
		return false;
	}

	// Can only occur at game start, need at least 2 waypoints for Monsters to function.
	if (waypoints.size() == 1)
	{
		return false;
	}

	// Are we on the targeted Waypoint?
	if (Distance(monster.position, waypoints[monster.waypoint_index].position) <= 2.0f)
	{
		// Have we reached last Waypoint?
		if (waypoints.size() - 1 == monster.waypoint_index)
		{
			// Deal damage to player then die.
			player_health -= monster.damage.value;
			return false;
		}

		// Target next Waypoint.
		++monster.waypoint_index;
	}

	const float xdir = waypoints[monster.waypoint_index].position.x - monster.position.x;
	const float ydir = waypoints[monster.waypoint_index].position.y - monster.position.y;
	const sf::Vector2f normalized_dir = Normalize(xdir, ydir);

	monster.position.x += (normalized_dir.x * MONSTER_SPEED * DeltaTime);
	monster.position.y += (normalized_dir.y * MONSTER_SPEED * DeltaTime);

	return true;
}

void UpdateTower(Tower& tower, float DeltaTime, const std::vector<Monster>& monsters, std::vector<Bullet>& bullets)
{
	tower.timer.value += DeltaTime;
	for (uint32_t i = 0; i < monsters.size(); ++i)
	{
		// Check if Monster is in range of Tower.
		if (Distance(tower.position, monsters[i].position) <= tower.range.value)
		{
			// Check if enough time has passed for us to fire again.
			if (tower.timer.value >= tower.attackRate.value)
			{
				// Don't worry about bullet velocity, as UpdateBullet() will handle that.
				bullets.emplace_back(Bullet({ tower.position.x, tower.position.y,	// Position
											  0.0f, 0.0f,							// Velocity
											  50,									// Damage
											  i }));								// Target Index

				// Reset timer to 0.0f as we just fired.
				tower.timer.value = 0.0f;

				return;
			}
		}
	}
}

// Returns false if Bullet hit a Monster, or there are no Monsters left.
bool UpdateBullet(Bullet& bullet, float DeltaTime, std::vector<Monster>& monsters)
{
	// No more monsters left, destroy bullet.
	if (monsters.size() == 0)
	{
		return false;
	}

	// If we were targetting the last Monster in monsters and they died, target the new last Monster.
	if (bullet.target_index >= monsters.size() && monsters.size() != 0)
	{
		bullet.target_index = (uint32_t)monsters.size() - 1;
	}

	// Get direction vectors to targeted Monster.
	const float xdir = monsters[bullet.target_index].position.x - bullet.position.x;
	const float ydir = monsters[bullet.target_index].position.y - bullet.position.y;

	const sf::Vector2f normalized_dir = Normalize(xdir, ydir);

	bullet.position.x += (normalized_dir.x * BULLET_SPEED * DeltaTime);
	bullet.position.y += (normalized_dir.y * BULLET_SPEED * DeltaTime);

	// Have we hit a monster?
	if (Distance(bullet.position, monsters[bullet.target_index].position) <= BULLET_RADIUS)
	{
		// Damage monster.
		monsters[bullet.target_index].health.value -= bullet.damage.value;

		return false;
	}

	return true;
}

int main(int argc, char** argv)
{
	sf::RenderWindow window(sf::VideoMode(WIDTH, HEIGHT, 32), "Tower Defense", sf::Style::Close);

	sf::Font liberation_mono_font;
	if (!liberation_mono_font.loadFromFile("liberation-mono.ttf"))
	{
		return -1;
	}
	uint32_t font_size = 24;

	sf::Text num_monsters_text("Monsters: ", liberation_mono_font, font_size);
	num_monsters_text.setPosition(10.0f, 10.0f);
	sf::Text num_waypoints_text("Waypoints: ", liberation_mono_font, font_size);
	num_waypoints_text.setPosition(10.0f, 40.0f);
	sf::Text num_towers_text("Towers: ", liberation_mono_font, font_size);
	num_towers_text.setPosition(10.0f, 70.0f);
	sf::Text monsters_killed_text("Kills: ", liberation_mono_font, font_size);
	monsters_killed_text.setPosition(10.0f, 100.0f);
	sf::Text player_health_text("Health: ", liberation_mono_font, font_size);
	player_health_text.setPosition(WIDTH / 2.0f - 100.0f, 10.0f);

	// Vectors containing all entities in the game.
	std::vector<Monster> monsters;
	std::vector<Waypoint> waypoints;
	std::vector<Tower> towers;
	std::vector<Bullet> bullets;

	// Set starting waypoint to ensure we have atleast one so Monsters can spawn.
	waypoints.emplace_back(Waypoint({ 150.0f, 150.0f }));

	uint32_t monsters_killed = 0;
	uint32_t player_health = 100;

	float Elapsed = 0.0f;
	float DeltaTime = 0.0f;
	sf::Clock clock;

	while (window.isOpen())
	{
		DeltaTime = clock.restart().asSeconds();
		Elapsed += DeltaTime;

		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
			{
				window.close();
			}

			if (event.type == sf::Event::KeyPressed)
			{
				if (event.key.code == sf::Keyboard::Escape)
				{
					window.close();
				}
				else if (event.key.code == sf::Keyboard::Space)
				{
					monsters.emplace_back(Monster({ 100,												// Health
													waypoints[0].position.x, waypoints[0].position.y,	// Position
													0.0f, 0.0f,											// Velocity
													0,													// Waypoint Index
													5 }));												// Damage
				}
			}
			else if (event.type == sf::Event::MouseButtonPressed)
			{
				const sf::Vector2i click_position = sf::Mouse::getPosition(window);
				if (event.mouseButton.button == sf::Mouse::Left)
				{
					waypoints.emplace_back(Waypoint({ (float)click_position.x, (float)click_position.y }));
				}
				else if (event.mouseButton.button == sf::Mouse::Right)
				{
					towers.emplace_back(Tower({ (float)click_position.x, (float)click_position.y,		// Position
												100.0f,													// AttackRange
												1.5f,													// AttackRate
												0.0f }));												// Timer
				}
			}
		}

		// Update monsters.
		for (uint32_t i = 0; i < monsters.size(); ++i)
		{
			if (!UpdateMonster(monsters[i], DeltaTime, waypoints, player_health))
			{
				// We are dead, remove Monster from vector.
				const size_t last = monsters.size() - 1;
				monsters[i].health.value = monsters[last].health.value;
				monsters[i].position.x = monsters[last].position.x;
				monsters[i].position.y = monsters[last].position.y;
				monsters[i].velocity.x = monsters[last].velocity.x;
				monsters[i].velocity.y = monsters[last].velocity.y;
				monsters[i].waypoint_index = monsters[last].waypoint_index;
				monsters[i].damage.value = monsters[last].damage.value;

				monsters.pop_back();

				// Increment monsters_killed.
				++monsters_killed;

				// Reduce i by 1 so we don't skip this copied monster.
				--i;
			}
		}

		// Update towers.
		for (uint32_t i = 0; i < towers.size(); ++i)
		{
			UpdateTower(towers[i], DeltaTime, monsters, bullets);
		}

		// Update bullets.
		for (uint32_t i = 0; i < bullets.size(); ++i)
		{
			if (!UpdateBullet(bullets[i], DeltaTime, monsters))
			{
				// We hit a Monster, swap element with last element in bullets vector
				// and call bullets.pop_back().
				const size_t last = bullets.size() - 1;
				bullets[i].position.x = bullets[last].position.x;
				bullets[i].position.y = bullets[last].position.y;
				bullets[i].damage.value = bullets[last].damage.value;
				bullets[i].velocity.x = bullets[last].velocity.x;
				bullets[i].velocity.y = bullets[last].velocity.y;
				bullets[i].target_index = bullets[last].target_index;

				bullets.pop_back();

				// Reduce i by 1 so we don't skip this copied bullet.
				--i;
			}
		}

		// If health == 0, game over!
		if (player_health == 0)
		{
			// Just return with value 1 right now, game over screen can be implemented later.
			return 1;
		}

		num_monsters_text.setString("Monsters: " + std::to_string(monsters.size()));
		num_waypoints_text.setString("Waypoints: " + std::to_string(waypoints.size()));
		num_towers_text.setString("Towers: " + std::to_string(towers.size()));
		monsters_killed_text.setString("Kills: " + std::to_string(monsters_killed));
		player_health_text.setString("Health: " + std::to_string(player_health));

		// Calculate ms/frame (16.67 = 60 FPS).
		static uint32_t count = 0;
		std::stringstream ss;
		ss << "Tower Defense - FPS: " << 1000.0f / Elapsed << " - Frame Time: " << Elapsed;
		// Don't update title every frame, this is expensive.
		// We have arbitrarily chosen to update once every 10 frames.
		if (count++ % 10 == 0)
		{
			window.setTitle(ss.str());
		}

		// Clear screen to light grey.
		window.clear(sf::Color(120, 120, 120, 255));

		// Draw entities.
		DrawWaypoints(waypoints, window);
		DrawMonsters(monsters, window);		// Draw Monsters after Waypoints so Monsters appear on top of Waypoints.
		DrawTowers(towers, window);
		DrawBullets(bullets, window);

		// Draw text.
		window.draw(num_monsters_text);
		window.draw(num_waypoints_text);
		window.draw(num_towers_text);
		window.draw(monsters_killed_text);
		window.draw(player_health_text);

		// Swap backbuffer to front.
		window.display();
	}

	return 0;
}