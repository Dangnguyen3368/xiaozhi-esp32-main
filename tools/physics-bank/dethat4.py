from common import *
# Đề thi thật, phần 3: các câu cần hình vẽ. Mỗi bài có hình (fig/b_<id>.png) hiện lên màn màu;
# đề vẫn tả mạch bằng lời để robot đọc được.
P("D115", 2, "KHTN Hà Nội 2014 – Tìm điện trở của ampe kế và vôn kế",
  "Đề thi vào 10 chuyên KHTN Hà Nội năm 2014, có hình trên màn. Mạch gồm hai nhánh song song: nhánh trên là R1 mắc song song với vôn kế V1, rồi nối tiếp ampe kế A1; nhánh dưới là R2 nối tiếp vôn kế V2. Hai nhánh nối chung rồi qua ampe kế A2 về cực âm. R1 = R2 = R, hai ampe kế giống nhau điện trở RA, hai vôn kế giống nhau điện trở RV. A1 chỉ 0,1 A, A2 chỉ 0,11 A, hai vôn kế cùng chỉ 9 V. Tính R, RA, RV và hiệu điện thế U giữa hai đầu mạch.",
  "RV = 900 Ω, R = 100 Ω, RA = 10 Ω, U = 11,1 V",
  ["Dòng qua nhánh dưới bằng 0,11 − 0,1 = 0,01 A, đó cũng là dòng qua V2.", "Hai vôn kế giống nhau cùng chỉ 9 V nên dòng qua V1 cũng 0,01 A, dòng qua R1 là 0,09 A."],
  "Dòng nhánh dưới 0,01 A qua V2 chỉ 9 V nên RV = 900 Ω. Dòng qua V1 cũng 0,01 A, qua R1 là 0,09 A, nên R = 9/0,09 = 100 Ω. Hiệu điện thế trên R2 là 0,01·100 = 1 V, nhánh dưới có 10 V. Nhánh trên cũng 10 V: 9 + 0,1·RA = 10, RA = 10 Ω. U = 10 + 0,11·10 = 11,1 V.",
  "abs(9/0.01-900)<1e-9 and abs(9/0.09-100)<1e-9 and abs((10-9)/0.1-10)<1e-9 and abs(10+0.11*10-11.1)<1e-9")
P("D116", 3, "Lam Sơn 2012 – Mạch đèn bảo vệ với hai công tắc ba cực",
  "Đề thi vào 10 chuyên Lam Sơn năm 2012, câu 5, có hình công tắc trên màn. Thiết kế hệ thống đèn bảo vệ gồm 3 bóng đèn và 2 công tắc ba cực (cực 1 nối được với cực 2 hoặc cực 3). Đèn 1 ở trước cổng, đèn 2 ngoài sân, đèn 3 ở cửa chính, hai công tắc đặt trong phòng ngủ. Người trong phòng điều khiển được để chỉ đèn 1 sáng, hoặc chỉ đèn 2, hoặc chỉ đèn 3, hoặc cả ba cùng sáng, các đèn sáng bình thường. Hãy nêu cách mắc và cách điều khiển.",
  "Mắc ba đèn nối tiếp Đ1, Đ2, Đ3 giữa hai cực nguồn L và R; cực 1 của K2 nối vào điểm giữa Đ1 và Đ2, cực 1 của K1 nối vào điểm giữa Đ2 và Đ3; cực 2 của mỗi công tắc nối về L, cực 3 nối về R. Chỉ Đ1: K2 và K1 cùng sang R. Chỉ Đ2: K2 sang L, K1 sang R. Chỉ Đ3: cả hai sang L. Cả ba: K2 sang R, K1 sang L.",
  ["Gọi hai đầu nguồn là L và R. Nối ba đèn thành một dãy L – Đ1 – a – Đ2 – b – Đ3 – R.", "Mỗi công tắc nối một điểm a hoặc b về L hoặc về R. Một đèn sáng khi hai đầu của nó nối vào hai cực khác nhau của nguồn."],
  "Mắc dãy L – Đ1 – a – Đ2 – b – Đ3 – R, với L, R là hai cực nguồn. Công tắc K2 có cực 1 nối a, K1 có cực 1 nối b; cực 2 nối L, cực 3 nối R. Đèn sáng khi hai đầu ở hai cực khác nhau, lúc đó nó có đủ hiệu điện thế U nên sáng bình thường. a và b cùng nối R: chỉ Đ1 sáng. a nối L, b nối R: chỉ Đ2 sáng. a và b cùng nối L: chỉ Đ3 sáng. a nối R, b nối L: cả ba đèn đều có đủ U, cùng sáng bình thường.",
  None)
P("D117", 3, "Hà Nội 2015 – Ngắm toàn thân trong gương nghiêng",
  "Đề thi vào 10 chuyên Hà Nội năm 2015, bài 4, có hình trên màn. Một cô gái cao 165 cm, mắt cách đỉnh đầu 10 cm, đứng gần chiếc gương phẳng lớn G đặt nghiêng 60 độ so với sàn nằm ngang, mép dưới gương chạm sàn. Câu 1: tìm khoảng cách xa nhất từ chân cô gái tới chân gương để cô ngắm được toàn thân, và chiều dài tối thiểu của gương khi đó. Câu 2: khi cô gái lùi xa dần thì ảnh của cô dịch chuyển thế nào?",
  "1) khoảng 89,5 cm (155/√3 cm), gương dài tối thiểu khoảng 93,7 cm tính từ mép dưới; 2) ảnh lùi xa và đi xuống, mất dần từ chân lên đầu",
  ["Mắt cao 155 cm. Ở vị trí xa nhất, tia từ mắt tới ảnh của chân đi qua đúng mép dưới gương.", "Khi đó ảnh M' của mắt nằm trên sàn, tam giác MCM' vuông tại C."],
  "1) Mắt M cao h = 155 cm. Xa nhất khi ảnh của mắt rơi xuống sàn tại M': tam giác MCM' vuông ở C, góc 60 độ, nên khoảng cách từ chân tới gương x = h/√3 ≈ 89,5 cm. Tia từ mắt tới ảnh của đỉnh đầu cắt gương tại K, tính được GK ≈ 93,7 cm, đó là chiều dài tối thiểu. 2) Khi lùi xa, ảnh lùi xa và đi xuống; phần chân ra khỏi vùng nhìn thấy trước, mất dần từ chân lên đầu.",
  "abs(155/sqrt(3)-89.49)<0.01")
P("D118", 2, "Quảng Bình 2013 – Vẽ tìm tiêu điểm thấu kính",
  "Đề thi vào 10 chuyên Quảng Bình năm 2013, câu 3 ý 1, có hình trên màn. Hình trên là một tia sáng đi qua thấu kính hội tụ, hình dưới là một tia sáng đi qua thấu kính phân kì; O là quang tâm, đường nằm ngang là trục chính. Hãy nêu cách vẽ để xác định tiêu điểm của mỗi thấu kính.",
  "Vẽ trục phụ qua O song song với tia tới; tia ló (hoặc đường kéo dài của tia ló với thấu kính phân kì) cắt trục phụ tại tiêu điểm phụ; từ đó hạ vuông góc xuống trục chính được tiêu điểm chính F'.",
  ["Các tia tới song song nhau thì tia ló (hay đường kéo dài) cùng đi qua một tiêu điểm phụ nằm trên tiêu diện.", "Tia đi qua quang tâm O truyền thẳng."],
  "Thấu kính hội tụ: vẽ đường thẳng qua O song song với tia tới (trục phụ). Tia ló cắt trục phụ tại tiêu điểm phụ F1'. Từ F1' hạ đường vuông góc xuống trục chính, chân đường vuông góc là tiêu điểm F'. Lấy F đối xứng F' qua O. Thấu kính phân kì: cũng vẽ trục phụ song song tia tới; kéo dài tia ló ngược về phía trước thấu kính, cắt trục phụ tại tiêu điểm phụ ảo; hạ vuông góc xuống trục chính được F'.",
  None)
P("D119", 3, "Vĩnh Phúc 2012 – Mạch có hai nguồn và ampe kế chỉ 0",
  "Đề thi vào 10 chuyên Vĩnh Phúc năm 2012, câu 2, có hình trên màn. Giữa A và B có ba nhánh: nhánh trên là nguồn U1 = 16 V (cực dương về phía A) nối tiếp r1 = 2 Ω; nhánh giữa là R1 nối A với N, R2 = 4 Ω nối N với B; nhánh dưới là R3 nối A với M, đèn 3 V – 3 W nối M với B. Giữa N và M có nguồn U2 = 5 V (cực dương ở N) nối tiếp r2 = 1 Ω và một ampe kế lí tưởng. a) Biết đèn sáng bình thường và ampe kế chỉ 0, tính R1, R3 và U_AB. b) Thay ampe kế bằng vôn kế lí tưởng, vôn kế chỉ bao nhiêu, đèn sáng thế nào?",
  "a) R1 = 1 Ω, R3 = 7 Ω, U_AB = 10 V; b) vôn kế chỉ 0, đèn vẫn sáng bình thường",
  ["Ampe kế chỉ 0: nhánh N – M không có dòng, nên U_NM = U2 = 5 V, dòng qua R1 bằng dòng qua R2, dòng qua R3 bằng dòng qua đèn (1 A).", "U_NM = U_NB − U_MB = I1·R2 − 3 = 5."],
  "a) Đèn: I = 1 A, U = 3 V. Không có dòng qua nhánh N – M nên U_NM = 5 V: 4·I1 − 3 = 5, I1 = 2 A. Dòng qua nguồn U1: I = 2 + 1 = 3 A, U_AB = 16 − 3·2 = 10 V. R1 + 4 = 10/2 nên R1 = 1 Ω. 10 = 1·R3 + 3 nên R3 = 7 Ω. b) Vôn kế lí tưởng cũng không cho dòng đi qua, các dòng không đổi, vôn kế chỉ U_NM − U2 = 0, đèn vẫn sáng bình thường.",
  "abs(4*2-3-5)<1e-9 and 16-3*2==10 and 10/2-4==1 and 10-3==7")
P("D120", 3, "Vĩnh Phúc 2012 – Hộp đen bốn đầu dây",
  "Đề thi vào 10 chuyên Vĩnh Phúc năm 2012, câu 4. Hộp đen có bốn đầu dây 1, 2, 3, 4, bên trong gồm các điện trở giống nhau R0. Đo được: R42 = 0; R14 = R12 = R43 = R32 = 5R0/3; R13 = 2R0/3. Xác định cách mắc đơn giản nhất bên trong hộp.",
  "Đầu 2 và 4 nối thẳng với nhau. Có 4 điện trở: một R0 nối 1 với 3; một R0 nối 1 với điểm X; một R0 nối 3 với X; một R0 nối X với đầu 2 (cũng là 4).",
  ["R42 = 0 nên đầu 2 và đầu 4 nối tắt, chỉ cần xét ba đầu 1, 2, 3.", "2R0/3 là R0 song song 2R0; 5R0/3 là R0 nối tiếp 2R0/3."],
  "R42 = 0 nên 2 và 4 nối thẳng. R13 = 2R0/3 = R0 song song với 2R0: giữa 1 và 3 có một R0, song song với hai R0 nối tiếp qua điểm X. R12 = R32 = 5R0/3 và đối xứng, nên đầu 2 nối vào X qua một R0. Kiểm tra: từ 2 qua R0 tới X, từ X tới 1 là R0 song song (R0 + R0) = 2R0/3, tổng 5R0/3. Đúng mọi số đo, dùng 4 điện trở.",
  "abs(1*2/(1+2)-2/3)<1e-12 and abs(1+1*2/(1+2)-5/3)<1e-12")
P("D121", 3, "Vĩnh Phúc 2016 – Gương nghiêng dựa góc tường",
  "Đề thi vào 10 chuyên Vĩnh Phúc năm 2016, câu 5 ý 2, có hình trên màn. Gương phẳng hình chữ nhật dài L = 2,5 m đặt trên sàn, mép dưới dựa vào góc tường, mặt gương hợp với sàn góc 60 độ, nghiêng ra phía trong phòng. Một người tiến dần về phía tường, mắt cách chân √3 m (khoảng 1,73 m). Khi cách tường bao nhiêu thì người đó bắt đầu nhìn thấy: a) ảnh của mắt mình; b) ảnh của chân mình?",
  "a) cách tường 2 m; b) cách tường khoảng 3,22 m (thấy chân trước khi thấy mắt)",
  ["Thấy ảnh của mắt khi đường vuông góc hạ từ mắt xuống mặt gương rơi vào trong gương. Đường vuông góc với gương tại mép trên cắt sàn cách góc tường 2L = 5 m.", "Thấy ảnh của chân khi tia từ mắt tới ảnh của chân cắt được gương, giới hạn là đi qua đúng mép trên của gương."],
  "Đặt gốc O ở góc tường. a) Đường vuông góc với gương tại mép trên A cắt sàn tại B với OB = L/cos 60° = 5 m; đường này hợp với sàn góc 30 độ. Mắt cao √3 m nằm trên đường này khi cách B một đoạn √3·√3 = 3 m, tức cách tường 5 − 3 = 2 m. Lại gần hơn thì thấy ảnh của mắt. b) Lấy ảnh C' của chân qua gương, cho tia từ mắt tới C' đi qua đúng mép trên của gương; giải ra người cách tường khoảng 3,22 m. Vậy khi tiến lại, người đó thấy ảnh của chân trước (từ khoảng 3,22 m), rồi mới thấy ảnh của mắt (từ 2 m).",
  "abs(5-sqrt(3)*sqrt(3)-2)<1e-9")
P("D122", 3, "Vĩnh Phúc 2017 – Đóng khóa K, tìm biến trở mới",
  "Đề thi vào 10 chuyên Vĩnh Phúc năm 2017, câu 4 ý b, có hình trên màn. Mạch như bài Đèn tối nhất: U = 8 V, r = 2 Ω, R2 = 3 Ω, đèn 3 Ω. Nguồn và r nối từ đầu B của biến trở tới điểm D; từ D, đèn đi tới đầu A của biến trở, R2 đi tới con chạy C. Khóa K và ampe kế lí tưởng nối thẳng đầu B với đầu A. Thay biến trở mới và đóng K. Khi đoạn AC bằng 6 Ω thì ampe kế chỉ 5/3 A. Tính điện trở toàn phần của biến trở mới.",
  "R' = 12 Ω",
  ["Đóng K thì A và B cùng một điểm. Hai đoạn CA = 6 Ω và CB = R' − 6 mắc song song giữa C và A.", "Ampe kế đo tổng dòng qua đèn và dòng qua đoạn CA đi về A."],
  "Đóng K: A ≡ B. Đoạn CA (6 Ω) song song CB (R' − 6), được Rc = 6(R' − 6)/R'. Từ D tới A có đèn 3 Ω song song với nhánh R2 + Rc. Thử R' = 12: Rc = 3, nhánh R2 + Rc = 6 Ω, song song đèn được 2 Ω, cả mạch 4 Ω, dòng chính 2 A, U_DA = 4 V. Dòng qua đèn 4/3 A, qua nhánh 2/3 A, trong đó qua CA một nửa là 1/3 A. Ampe kế chỉ 4/3 + 1/3 = 5/3 A. Vậy R' = 12 Ω.",
  "abs(4/3+(4/6)*(6/12)-5/3)<1e-12")

TOPICS = {"D115": "dien", "D116": "dien", "D117": "quang", "D118": "quang", "D119": "dien",
          "D120": "dien", "D121": "quang", "D122": "dien"}
# Problem id -> figure name (fig/b_<name>.png). D62 (part a of the same exam question) reuses D72's.
FIGURES = {"D115": "D115", "D116": "D116", "D117": "D117", "D118": "D118", "D119": "D119", "D121": "D121",
           "D122": "D122", "D62": "D122"}
# Problem id -> data lines shown on screen (under the figure when there is one), so the student
# can read the numbers while the robot talks. Names match the labels drawn on the figure.
GIVENS = {
    "D115": ["R1 = R2 = R", "A1 chỉ 0,1 A;  A2 chỉ 0,11 A", "V1 và V2 cùng chỉ 9 V", "Tìm R, RA, RV và U"],
    "D116": ["Đ1: cổng, Đ2: sân, Đ3: cửa chính", "K1, K2: cực 1 nối cực 2 hoặc 3", "Bật riêng Đ1, Đ2, Đ3 hoặc cả ba"],
    "D117": ["Cao 165 cm, mắt cách đỉnh 10 cm", "Gương G nghiêng 60° so với sàn", "Tìm CG lớn nhất, gương ngắn nhất"],
    "D118": ["O: quang tâm; ngang: trục chính", "Trên: TK hội tụ, dưới: TK phân kì", "Vẽ tìm tiêu điểm F, F'"],
    "D119": ["U1 = 16 V, r1 = 2 Ω, R2 = 4 Ω", "U2 = 5 V, r2 = 1 Ω", "Đèn Đ: 3 V – 3 W, sáng bình thường", "A chỉ 0. Tìm R1, R3, U_AB"],
    "D121": ["Gương dài L = 2,5 m, α = 60°", "Mắt cách chân √3 m ≈ 1,73 m", "Cách tường bao nhiêu thì thấy", "ảnh của mắt? ảnh của chân?"],
    "D120": ["Hộp đen 4 đầu 1, 2, 3, 4; các R0", "R42 = 0; R13 = 2R0/3", "R14 = R12 = R43 = R32 = 5R0/3", "Tìm cách mắc đơn giản nhất"],
    "D122": ["U = 8 V, r = 2 Ω, R2 = 3 Ω", "Đèn R1 = 3 Ω, khóa K đóng", "AC = 6 Ω, ampe kế chỉ 5/3 A", "Tìm điện trở toàn phần R'"],
}
