#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <ctime>
#include <algorithm>
#include <cppconn/prepared_statement.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>
#include <mysql_connection.h>
#include <mysql_driver.h>
using namespace std;
sql::mysql::MySQL_Driver* driver;
sql::Connection* con;

string currentUsername;

enum GameState
{
	LOGIN_SCREEN,
	GAME_SCREEN
};
GameState currentState = LOGIN_SCREEN;
sf::RenderWindow window;
sf::Font font;
//Kết nối CSDL
void connectDB() {
	try
	{
		driver = sql::mysql::get_mysql_driver_instance();
		con = driver->connect("tcp://127.0.0.1:3308",
			"root", "123456");
		con->setSchema("flashcard_db");
		cout << "OK!" << endl;
	}
	catch (sql::SQLException& e)
	{
		std::cerr << "SQL Error: " << e.what() << std::endl;
	}
}

//Thẻ bài
struct Card {
	sf::Sprite sprite;
	sf::RectangleShape back;
	bool revealed = false;
	bool matched = false;
	int id;
	bool flipping = false;
	float flipProgress = 0.f;
	bool showingFront = false;
	float originalScaleX;
	sf::Vector2f originalPos;
	sf::Vector2f backBasePos;
};

//Kiểm tra người dùng
bool checkUser(const string& username) {
	if (!con || username.empty()) return false;

	try
	{
		unique_ptr<sql::PreparedStatement> pstmt(
			con->prepareStatement("SELECT COUNT(*) FROM player WHERE name = ?")
		);
		pstmt->setString(1, username);
		unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		if (res->next() && res->getInt(1) > 0) {
			cout << "User '" << username << "' found. Access granted";

			//Tạo user mới nếu không tồn tại
			unique_ptr<sql::Statement> stmt(con->createStatement());
			stmt->execute("INSERT INTO player (name) VALUES ('" + username + "')");
			cout << "User '" << username << "' created." << endl;
			return true;
		}
	}
	catch (sql::SQLException& e)
	{
		cerr << "SQL Error during user check: " << e.what() << endl;
		return false;
	}
}

//Hàm xử lý màn hình đăng nhập
void runLoginScreen() {
	//Khởi tạo form
	static string input_text;
	static sf::Text username_prompt("Enter name: ", font, 30);
	static sf::RectangleShape input_box({ 300, 40 });
	static sf::Text username_input(input_text, font, 30);
	static sf::RectangleShape login_button({ 200, 50 });
	static sf::Text button_text("START GAME", font, 30);

	//Vị trí
	username_prompt.setPosition(200, 200);
	input_box.setPosition(450, 200);
	username_input.setPosition(460, 200);
	login_button.setPosition(300, 300);
	button_text.setPosition(300 + 100 - button_text.getGlobalBounds().width / 2,
		300 + 25 - button_text.getGlobalBounds().height + 5);

	input_box.setFillColor(sf::Color(30, 30, 30));
	login_button.setFillColor(sf::Color::Green);
	username_prompt.setFillColor(sf::Color::White);
	username_input.setFillColor(sf::Color::White);
	button_text.setFillColor(sf::Color::White);

	sf::Event event;
	while (window.pollEvent(event)) {
		if (event.type == sf::Event::Closed) window.close();

		// Xử lý nhập liệu
		if (event.type == sf::Event::TextEntered) {
			if (event.text.unicode < 128) {
				if (event.text.unicode == '\b' && !input_text.empty()) {
					input_text.pop_back();
				}
				else if (event.text.unicode != '\b' && event.text.unicode != '\r' && event.text.unicode != '\n' && input_text.length() < 15) {
					input_text += static_cast<char>(event.text.unicode);
				}
				username_input.setString(input_text);
			}
		}

		// Xử lý click
		if (event.type == sf::Event::MouseButtonPressed) {
			if (event.mouseButton.button == sf::Mouse::Left) {
				sf::Vector2f mousePos = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
				if (login_button.getGlobalBounds().contains(mousePos)) {
					if (checkUser(input_text)) {
						currentUsername = input_text;
						currentState = GAME_SCREEN; // <-- CHUYỂN TRẠNG THÁI
						// Thay đổi kích thước cửa sổ cho Game Screen
						window.setSize(sf::Vector2u(1300, 600));
						window.setTitle("Flashcard Match Game - " + currentUsername);
					}
				}
			}
		}
	}

	// Vẽ
	window.clear(sf::Color(100, 100, 100));
	window.draw(username_prompt);
	window.draw(input_box);
	window.draw(username_input);
	window.draw(login_button);
	window.draw(button_text);
}

void runGameScreen() {
	// 1. KHAI BÁO CÁC BIẾN STATIC (CẦN GIỮ NGUYÊN)
	static bool isInitialized = false;
	static sf::Texture textures[8]; // <-- SỬ DỤNG BIẾN NÀY
	static std::vector<Card> cards; // <-- SỬ DỤNG BIẾN NÀY
	static sf::Text winMessage; 	// <-- SỬ DỤNG BIẾN NÀY
	static sf::Text turnText; 		// <-- SỬ DỤNG BIẾN NÀY
	static sf::Text ruleText; 		// <-- SỬ DỤNG BIẾN NÀY
	static int matchedPairs = 0;
	static bool gameWon = false;
	static int first = -1, second = -1;
	static sf::Clock timer;
	static bool waiting = false;
	static int turnCount = 0;

	// Đảm bảo biến font toàn cục đã được load từ main()
	extern sf::Font font;
	extern string currentUsername;
	extern sf::RenderWindow window;

	if (!isInitialized) {
		// --- LOGIC KHỞI TẠO GAME (Chỉ chạy 1 lần) ---

		// BỎ KHAI BÁO CỤC BỘ: sf::Texture textures[8];
		// BỎ KHAI BÁO CỤC BỘ: vector<Card> cards;

		// Load ảnh vào biến textures[8] static
		for (int i = 0; i < 8; i++)
			textures[i].loadFromFile("images/" + to_string(i) + ".jpg");

		// Tạo danh sách id và xáo trộn (GIỮ NGUYÊN)
		vector<int> ids;
		for (int i = 0; i < 8; i++) {
			ids.push_back(i);
			ids.push_back(i);
		}
		srand((unsigned)time(0));
		random_shuffle(ids.begin(), ids.end());

		// Tạo 16 thẻ và đẩy vào biến cards static
		float cardWidth = 120, cardHeight = 120, spacing = 20;

		float totalWidth = 4 * cardWidth + 3 * spacing;
		float totalHeight = 4 * cardHeight + 3 * spacing;
		// Kích thước window đã đổi thành 1300x600 khi vào GameScreen
		float startX = (1300 - totalWidth) / 2;
		float startY = (600 - totalHeight) / 2;

		for (int i = 0; i < 16; i++) {
			Card c;
			c.id = ids[i];
			// Dùng texture từ mảng static textures[8]
			c.sprite.setTexture(textures[c.id]);

			// Scale ảnh cho vừa khung
			sf::Vector2u size = textures[c.id].getSize();
			float scale = min(cardWidth / size.x, cardHeight / size.y);
			c.sprite.setScale(scale, scale);

			// Tính toán vị trí (Giữ nguyên)
			int col = i % 4;
			int row = i / 4;
			float x = startX + col * (cardWidth + spacing);
			float y = startY + row * (cardWidth + spacing);

			float offsetX = (cardWidth - size.x * scale) / 2;
			float offsetY = (cardHeight - size.y * scale) / 2;
			c.originalScaleX = scale;
			c.originalPos = sf::Vector2f(x + offsetX, y + offsetY);

			c.sprite.setPosition(c.originalPos);

			// Tạo mặt sau
			c.back.setSize(sf::Vector2f(cardWidth, cardHeight));
			c.back.setPosition(x, y);
			c.backBasePos = sf::Vector2f(x, y);
			c.back.setFillColor(sf::Color(180, 180, 180));
			c.back.setOutlineColor(sf::Color::Black);
			c.back.setOutlineThickness(2);

			cards.push_back(c); // Đẩy vào vector cards static
		}

		// BỎ KHAI BÁO CỤC BỘ: sf::Font font; (Dùng biến toàn cục)
		// BỎ KHAI BÁO CỤC BỘ: sf::Text winMessage; (Dùng biến static)

		// Khởi tạo winMessage static
		winMessage.setFont(font);
		winMessage.setString(L"CHÚC MỪNG! BẠN ĐÃ THẮNG!");
		winMessage.setCharacterSize(40);
		winMessage.setFillColor(sf::Color::Red);
		winMessage.setStyle(sf::Text::Bold);

		// Khởi tạo turnText static
		turnText.setFont(font);
		turnText.setCharacterSize(40);
		turnText.setPosition(1100, 10);
		turnText.setFillColor(sf::Color::Red);
		turnText.setStyle(sf::Text::Bold);

		// Khởi tạo ruleText static
		ruleText.setFont(font);
		ruleText.setCharacterSize(20);
		ruleText.setFillColor(sf::Color::Red);
		ruleText.setPosition(20, 50);

		//Nội dung luật chơi (Thêm tên người dùng)
		ruleText.setString(
			L"LUẬT CHƠI:\n"
			L"- Click để lật 2 lá bài.\n"
			L"- Đang chơi: " + sf::String::fromUtf8(currentUsername.begin(), currentUsername.end())
		);


		//Căn giữa thông báo chiến thắng
		sf::FloatRect textRect = winMessage.getLocalBounds();
		winMessage.setOrigin(textRect.left + textRect.width / 2.0f, textRect.top + textRect.height / 2.0f);
		winMessage.setPosition(sf::Vector2f(1300 / 2.0f, 600 / 2.0f)); // Căn giữa trên màn hình 1300x600

		// BỎ KHAI BÁO CỤC BỘ: int matchedPairs = 0, bool gameWon = false, v.v.
		// Đã được khởi tạo 1 lần ở đầu hàm static.

		isInitialized = true; // <-- Thiết lập cờ để không chạy lại khối này
	} // --- KẾT THÚC KHỐI KHỞI TẠO ---

	// --- LOGIC VÒNG LẶP GAME (Event, Update, Draw) ---

	// 2. XỬ LÝ SỰ KIỆN (EVENT)
	sf::Event e;
	while (window.pollEvent(e))
		if (e.type == sf::Event::Closed) window.close();

	// 3. XỬ LÝ CLICK CHUỘT
	// Logic lật bài đã được gộp lại để tránh lặp lại code
	if (!waiting && sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
		sf::Vector2f mouse(sf::Mouse::getPosition(window));
		for (int i = 0; i < cards.size(); i++) {
			if (cards[i].back.getGlobalBounds().contains(mouse) && !cards[i].revealed && !cards[i].matched) {
				cards[i].revealed = true;
				cards[i].flipping = true;

				if (first == -1) first = i;
				else if (second == -1 && i != first) {
					second = i;
					waiting = true;
					timer.restart();
				}
				break;
			}
		}
	}

	// 4. CẬP NHẬT HIỆU ỨNG LẬT VÀ TRẠNG THÁI
	// Cập nhật hiệu ứng lật bài (GIỮ NGUYÊN)
	for (int i = 0; i < cards.size(); i++)
	{
		Card& c = cards[i];
		if (c.flipping) {
			c.flipProgress += 0.03f;

			if (c.flipProgress >= 0.6f && !c.showingFront) {
				c.showingFront = true;
			}

			if (c.flipProgress >= 1.f) {
				c.flipProgress = 0.f;
				c.flipping = false;
				c.revealed = true;
				c.showingFront = true;

				// LOẠI BỎ logic gán first/second ở đây. Việc này đã được xử lý trong Event Loop (Click chuột).
				// Nếu giữ lại, nó sẽ gán first/second lại khi hiệu ứng lật hoàn tất, dẫn đến logic game bị hỏng.
			}
		}
	}

	// Xử lý so khớp (GIỮ NGUYÊN)
	if (waiting && timer.getElapsedTime().asSeconds() > 1) {
		turnCount++;
		if (cards[first].id == cards[second].id) {
			cards[first].matched = cards[second].matched = true;
			matchedPairs++;
		}
		else {
			cards[first].revealed = cards[second].revealed = false;
			// Kích hoạt lật lại sau khi so khớp sai
			cards[first].flipping = cards[second].flipping = true;
			cards[first].showingFront = cards[second].showingFront = true; // Bắt đầu lật ngược từ mặt trước
		}

		first = second = -1;
		waiting = false;
	}

	// Kiểm tra thắng (GIỮ NGUYÊN)
	if (!gameWon && matchedPairs == 8) {
		gameWon = true;
	}

	// Cập nhật text (GIỮ NGUYÊN)
	turnText.setString("Turns: " + to_string(turnCount));

	// 5. VẼ (DRAW)
	window.clear(sf::Color::White);
	window.draw(ruleText);
	window.draw(turnText); // Vẽ TurnText

	for (auto& c : cards) {
		if (c.matched) continue;

		float scaleX = 1.f;
		if (c.flipping)
			scaleX = 1.f - abs(c.flipProgress - 0.5f) * 2.f;

		if (c.revealed) { // Nếu đã lật (revealed) hoặc đang lật (flipping)
			c.sprite.setScale(scaleX * c.originalScaleX, c.originalScaleX);

			float cardWidth = 120;
			float cardBackX = c.back.getPosition().x;
			float currentTotalWidth = cardWidth * scaleX;
			float diffX = (cardWidth - currentTotalWidth) / 2.f;
			float newPos = cardBackX + diffX + (c.originalPos.x - cardBackX);

			c.sprite.setPosition(newPos, c.originalPos.y);

			window.draw(c.sprite);
		}
		else { // Thẻ chưa lật (showing back)
			c.back.setScale(scaleX, 1.f);

			float cardWidth = 120;
			float currentTotalWidth = cardWidth * scaleX;
			float diffX = (cardWidth - currentTotalWidth) / 2.f;

			c.back.setPosition(c.backBasePos.x + diffX, c.backBasePos.y);

			window.draw(c.back);
		}
	}
	if (gameWon) {
		window.draw(winMessage);
	}
}
int main() {
	// 1. Kết nối CSDL
	connectDB();

	// 2. KHỞI TẠO CỬA SỔ VÀ FONT (cho màn hình LOGIN)
	window.create(sf::VideoMode(1300, 600), "Login Flashcard");
	window.setFramerateLimit(60);

	// Load Font
	if (!font.loadFromFile("font/arial.ttf")) {
		std::cerr << "❌ ERROR: Failed to load font!" << std::endl;
		return -1;
	}

	// 3. VÒNG LẶP CHÍNH
	while (window.isOpen()) {
		if (currentState == LOGIN_SCREEN) {
			runLoginScreen(); // Xử lý đăng nhập
		}
		else if (currentState == GAME_SCREEN) {
			runGameScreen(); // Xử lý trò chơi
		}

		window.display(); // Vẽ khung hình ra màn hình
	}

	// Dọn dẹp
	if (con) delete con;

	return 0;
}