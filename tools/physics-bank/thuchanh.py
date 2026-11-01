from common import *
P("T01", 2, "Đo khối lượng riêng của hòn đá",
  "Cho một lực kế, một cốc nước, sợi chỉ và một hòn đá không thấm nước. Biết khối lượng riêng của nước. Trình bày cách xác định khối lượng riêng của hòn đá.",
  "D_đá = D_nước·P/(P − P'), với P là số chỉ ngoài không khí, P' là số chỉ khi nhúng chìm trong nước",
  ["Lực kế cho trọng lượng hòn đá. Lực kế khi nhúng trong nước cho lực đẩy Ác-si-mét.", "Từ lực đẩy suy ra thể tích đá."],
  "Bước 1: treo đá vào lực kế ngoài không khí, đọc P. Bước 2: nhúng chìm hoàn toàn hòn đá trong nước, không chạm đáy, đọc P'. Bước 3: F_A = P − P' = 10·D_nước·V, nên V = (P − P')/(10·D_nước). Khối lượng m = P/10. D = m/V = D_nước·P/(P − P'). Làm nhiều lần rồi lấy trung bình.")
P("T02", 3, "Đo khối lượng riêng của dầu bằng ống chữ U",
  "Cho một ống chữ U, nước đã biết khối lượng riêng, dầu không hòa tan trong nước và một thước đo. Trình bày cách xác định khối lượng riêng của dầu.",
  "D_dầu = D_nước·h_nước/h_dầu",
  ["Đổ nước vào trước, rồi đổ dầu vào một nhánh.", "Xét hai điểm ở cùng độ cao: mặt phân cách dầu nước và điểm tương ứng ở nhánh kia."],
  "Đổ nước vào ống chữ U, sau đó đổ dầu vào một nhánh. Đo chiều cao cột dầu h_dầu tính từ mặt phân cách, và chiều cao cột nước h_nước ở nhánh kia tính từ cùng mức mặt phân cách. Áp suất bằng nhau: D_dầu·h_dầu = D_nước·h_nước, nên D_dầu = D_nước·h_nước/h_dầu.")
P("T03", 3, "Đo khối lượng riêng chất lỏng bằng đòn bẩy",
  "Cho một thanh cứng đồng chất, giá đỡ làm điểm tựa, một vật nặng, một quả cân, thước đo, cốc nước và cốc chất lỏng X. Biết khối lượng riêng của nước. Trình bày cách đo khối lượng riêng của chất lỏng X.",
  "D_X = D_nước·(l − l2)/(l − l1), với l, l1, l2 là cánh tay đòn của quả cân khi vật ở ngoài không khí, trong nước và trong chất lỏng X",
  ["Treo vật một đầu, quả cân đầu kia, điểm tựa ở giữa thanh để trọng lượng thanh không gây mô men. Dịch quả cân để cân bằng, ghi cánh tay đòn.", "Làm ba lần: vật ở ngoài không khí, vật nhúng trong nước, vật nhúng trong chất lỏng X."],
  "Đặt điểm tựa ở trung điểm thanh. Treo vật ở khoảng cách a cố định, dịch quả cân P0 đến khi cân bằng. Ngoài không khí, cánh tay đòn quả cân là l: P·a = P0·l. Trong nước là l1: (P − F1)·a = P0·l1. Trong X là l2: (P − F2)·a = P0·l2. Suy ra F1/P = 1 − l1/l và F2/P = 1 − l2/l. Vậy D_X/D_nước = F2/F1 = (l − l2)/(l − l1).")
P("T04", 2, "Đo nhiệt dung riêng của kim loại",
  "Cho một nhiệt lượng kế, nước đã biết nhiệt dung riêng, nhiệt kế, cân, bếp đun và một miếng kim loại. Trình bày cách đo nhiệt dung riêng của kim loại, bỏ qua nhiệt dung của nhiệt lượng kế.",
  "c = m_n·c_n·(t − t1)/(m·(t2 − t))",
  ["Đun nóng miếng kim loại trong nước sôi đến nhiệt độ t2 đã biết.", "Thả vào nước lạnh ở t1, đo nhiệt độ cân bằng t."],
  "Cân miếng kim loại được m, cân nước được m_n. Đo nhiệt độ nước t1. Nhúng kim loại vào nước sôi một lúc để kim loại đạt t2 bằng 100 độ C. Thả nhanh kim loại vào nhiệt lượng kế, khuấy đều, đọc nhiệt độ cân bằng t. Cân bằng nhiệt: m·c·(t2 − t) = m_n·c_n·(t − t1), suy ra c.")
P("T05", 3, "Đo nhiệt nóng chảy của nước đá",
  "Cho nhiệt lượng kế đã biết nhiệt dung, nước ấm, nước đá đang tan ở 0 độ C, cân và nhiệt kế. Trình bày cách đo nhiệt nóng chảy của nước đá.",
  "λ = [(m_n·c_n + C)(t1 − t) − m_đ·c_n·t]/m_đ",
  ["Đo khối lượng nước ấm và nhiệt độ ban đầu t1.", "Thả nước đá vào, đợi tan hết, đo nhiệt độ cuối t, rồi cân lại để biết khối lượng nước đá."],
  "Cân nước ấm được m_n, đo t1. Thả cục nước đá đã lau khô vào, khuấy đến khi tan hết, đọc nhiệt độ cuối t. Cân lại toàn bộ để biết khối lượng nước đá m_đ. Cân bằng nhiệt: (m_n·c_n + C)(t1 − t) = m_đ·λ + m_đ·c_n·(t − 0), suy ra λ.")
P("T06", 2, "Đo điện trở bằng vôn kế và ampe kế",
  "Trình bày cách đo điện trở của một dây dẫn bằng vôn kế và ampe kế: vẽ sơ đồ bằng lời, cách mắc và cách tính.",
  "Ampe kế nối tiếp, vôn kế song song với điện trở; R = U/I",
  ["Ampe kế mắc nối tiếp với điện trở, vôn kế mắc song song với điện trở.", "Thay đổi hiệu điện thế vài lần để lấy trung bình."],
  "Mắc nguồn, khóa K, ampe kế và điện trở nối tiếp. Mắc vôn kế song song với hai đầu điện trở, chú ý cực dương nối về phía cực dương nguồn. Đóng K, đọc U và I, tính R = U/I. Thay đổi nguồn ba lần, tính giá trị trung bình. Vì ampe kế và vôn kế không lí tưởng nên có sai số do cách mắc.")
P("T07", 3, "Đo điện trở bằng một ampe kế",
  "Cho một nguồn có hiệu điện thế không đổi nhưng chưa biết, một ampe kế lí tưởng, một điện trở đã biết R0, một điện trở cần đo Rx, dây nối và khóa. Trình bày cách xác định Rx.",
  "Rx = R0·I0/Ix",
  ["Lần lượt mắc từng điện trở với ampe kế vào nguồn.", "Hiệu điện thế như nhau nên dòng điện tỉ lệ nghịch với điện trở."],
  "Mắc R0 nối tiếp ampe kế vào nguồn, đọc I0, ta có U = I0·R0. Thay R0 bằng Rx, đọc Ix, ta có U = Ix·Rx. Vậy Rx = R0·I0/Ix.")
P("T08", 3, "Đo điện trở của vôn kế",
  "Cho một nguồn có hiệu điện thế không đổi, một vôn kế cần đo điện trở và một điện trở R0 đã biết, có độ lớn cỡ điện trở vôn kế. Trình bày cách đo điện trở vôn kế.",
  "R_V = R0·U1/(U − U1)",
  ["Mắc vôn kế trực tiếp vào nguồn để biết U.", "Mắc vôn kế nối tiếp với R0 rồi vào nguồn, vôn kế chỉ U1, chính là hiệu điện thế trên vôn kế."],
  "Lần 1: vôn kế mắc thẳng vào nguồn chỉ U. Lần 2: vôn kế nối tiếp với R0, vôn kế chỉ U1, R0 nhận U − U1. Dòng như nhau: U1/R_V = (U − U1)/R0, nên R_V = R0·U1/(U − U1).")
P("T09", 2, "Đo tiêu cự thấu kính hội tụ",
  "Cho một thấu kính hội tụ, một vật sáng, một màn ảnh và thước. Trình bày một cách đo tiêu cự thấu kính.",
  "Dịch sao cho ảnh rõ, bằng vật; khi đó f = L/4 với L là khoảng cách vật màn",
  ["Khi vật đặt cách thấu kính 2f thì ảnh thật bằng vật, cách thấu kính 2f.", "Một cách khác: hứng ảnh của một vật ở rất xa thì ảnh nằm tại tiêu điểm."],
  "Đặt vật và màn ở hai bên thấu kính, dịch vật và màn ra xa thấu kính những đoạn bằng nhau cho đến khi thu được ảnh rõ nét, cao bằng vật. Khi đó d = d' = 2f, đo khoảng cách L giữa vật và màn, f = L/4. Cách nhanh: hứng ảnh của cửa sổ ở xa lên màn, khoảng cách từ thấu kính đến màn gần bằng f.")
P("T10", 3, "Đo tiêu cự thấu kính phân kì",
  "Cho một thấu kính phân kì, một thấu kính hội tụ đã biết tiêu cự, vật sáng, màn và thước. Trình bày cách đo tiêu cự thấu kính phân kì.",
  "Dùng thấu kính hội tụ tạo ảnh thật, dùng ảnh đó làm vật ảo cho thấu kính phân kì, rồi tính f từ hai lần đo",
  ["Thấu kính phân kì không cho ảnh thật của vật thật, nên cần thấu kính hội tụ giúp.", "Đặt thấu kính phân kì giữa thấu kính hội tụ và ảnh thật của nó."],
  "Dùng thấu kính hội tụ cho ảnh thật A1 trên màn, ghi vị trí. Đặt thấu kính phân kì giữa thấu kính hội tụ và A1, cách A1 một đoạn a. Dịch màn ra xa đến khi thu được ảnh rõ, cách thấu kính phân kì một đoạn b. A1 là vật ảo đối với thấu kính phân kì, cho ảnh thật: 1/f = 1/a − 1/b, nên f = a·b/(b − a).")
P("T11", 1, "Đo tốc độ trung bình",
  "Trình bày cách đo tốc độ trung bình của một xe đồ chơi chạy trên mặt bàn, dùng thước mét và đồng hồ bấm giây.",
  "Đo quãng đường s và thời gian t, v = s/t; làm nhiều lần lấy trung bình",
  ["Đánh dấu vạch xuất phát và vạch đích."],
  "Đánh dấu hai vạch cách nhau s, đo bằng thước. Bấm đồng hồ khi xe qua vạch đầu và dừng khi qua vạch sau, được t. Tính v = s/t. Làm 3 đến 5 lần, lấy trung bình để giảm sai số.")
P("T12", 1, "Đo đường kính sợi dây nhỏ",
  "Chỉ với một thước có độ chia nhỏ nhất 1 mm và một chiếc bút chì, làm sao đo được đường kính một sợi dây đồng rất mảnh?",
  "Quấn nhiều vòng sát nhau, đo chiều dài cuộn rồi chia cho số vòng",
  ["Một sợi thì quá nhỏ để đo, nhưng nhiều sợi xếp liền nhau thì đo được."],
  "Quấn sợi dây sít nhau quanh bút chì, ví dụ 50 vòng. Đo chiều dài L của phần quấn. Đường kính d = L/50.")
P("T13", 3, "Đo khối lượng của chiếc thước",
  "Cho một chiếc thước dài đồng chất có vạch chia, một quả cân khối lượng m đã biết và một giá đỡ hình nêm. Trình bày cách xác định khối lượng của thước.",
  "M = m·a/b",
  ["Trọng lượng thước đặt tại trung điểm của thước.", "Đặt quả cân ở một đầu, dịch thước trên nêm đến khi cân bằng."],
  "Đặt quả cân ở đầu thước, dịch thước trên nêm đến khi thước cân bằng. Đo khoảng cách a từ điểm tựa đến quả cân, và b từ điểm tựa đến trung điểm của thước. Cân bằng mô men: m·a = M·b, nên M = m·a/b.")
P("T14", 2, "Kiểm tra vật rỗng hay đặc",
  "Có một quả cầu nhôm, biết khối lượng riêng của nhôm là 2700 kg/m³. Dùng cân và bình chia độ, làm sao biết quả cầu rỗng hay đặc? Nếu cân được 270 g và thể tích đo được 120 cm³ thì kết luận gì?",
  "Quả cầu rỗng, phần rỗng 20 cm³",
  ["Tính thể tích của 270 g nhôm đặc rồi so sánh với thể tích đo được."],
  "Cân được m, đo thể tích V bằng bình chia độ. So sánh m/V với D_nhôm. Với số liệu đã cho: 270 g nhôm đặc có thể tích 100 cm³, nhỏ hơn 120 cm³, nên quả cầu rỗng, phần rỗng 20 cm³.",
  "270/2.7==100")
P("T15", 3, "Đo chiết suất khối thủy tinh",
  "Cho một khối thủy tinh hai mặt song song, giấy trắng, ghim, thước đo góc. Trình bày cách đo chiết suất thủy tinh.",
  "n = sin i/sin r, đo i và r bằng thước đo góc",
  ["Cắm hai ghim để tạo tia tới, nhìn qua khối kính cắm hai ghim khác thẳng hàng với ảnh của hai ghim đầu.", "Vẽ pháp tuyến tại điểm tới, nối các điểm để có tia khúc xạ trong kính."],
  "Đặt khối kính lên giấy, vạch mép. Cắm ghim P1, P2 tạo tia tới. Nhìn qua mặt bên kia, cắm P3, P4 sao cho thấy chúng thẳng hàng với ảnh P1, P2. Bỏ khối kính, nối P3P4 để có tia ló, nối điểm tới và điểm ló để có tia khúc xạ. Đo góc tới i và góc khúc xạ r, tính n = sin i/sin r. Làm với vài góc tới và lấy trung bình.")
P("T16", 3, "Đo nhiệt dung riêng của nước bằng dụng cụ điện",
  "Cho một dây đun điện, nguồn, vôn kế, ampe kế, đồng hồ, nhiệt kế, cân và cốc cách nhiệt. Trình bày cách đo nhiệt dung riêng của nước, có tính đến nhiệt hao phí.",
  "Làm hai lần với hai khối lượng nước khác nhau để khử hao phí: c = (U·I·t2 − U·I·t1)/((m2 − m1)·Δt)",
  ["Q = U·I·t cung cấp cho nước và một phần bị hao phí.", "Làm hai lần cùng độ tăng nhiệt độ và cùng thời gian gần nhau để hao phí xấp xỉ như nhau."],
  "Lần 1: nước khối lượng m1, đun cho nhiệt độ tăng Δt, mất thời gian t1. Lần 2: nước m2, cùng Δt, mất t2. Coi hao phí và nhiệt dung cốc như nhau: U·I·t1 = (m1·c + C)Δt + Q_hp và U·I·t2 = (m2·c + C)Δt + Q_hp. Trừ hai phương trình: c = U·I·(t2 − t1)/((m2 − m1)·Δt).")
P("T17", 2, "Tính sai số phép đo",
  "Đo chiều dài một vật 5 lần được: 12,1; 12,3; 12,2; 12,4; 12,0 cm. Tính giá trị trung bình và sai số tuyệt đối trung bình.",
  "Trung bình 12,2 cm; sai số khoảng 0,12 cm",
  ["Tính trung bình cộng 5 lần đo.", "Sai số mỗi lần là trị tuyệt đối của hiệu so với trung bình, rồi lấy trung bình các sai số đó."],
  "Trung bình: 61/5 = 12,2 cm. Sai số từng lần: 0,1; 0,1; 0; 0,2; 0,2. Trung bình sai số: 0,6/5 = 0,12 cm. Kết quả: l = 12,2 ± 0,12 cm.",
  "abs(sum([12.1,12.3,12.2,12.4,12.0])/5-12.2)<1e-9 and abs(sum(abs(x-12.2) for x in [12.1,12.3,12.2,12.4,12.0])/5-0.12)<1e-9")
P("T18", 3, "Đo điện trở trong của nguồn",
  "Một nguồn điện được coi như một nguồn hiệu điện thế không đổi U0 mắc nối tiếp với điện trở trong r. Cho một vôn kế có điện trở rất lớn và một điện trở R đã biết. Trình bày cách xác định U0 và r.",
  "Đo U0 khi hở mạch; đo U1 khi mắc R; r = R·(U0 − U1)/U1",
  ["Vôn kế điện trở rất lớn mắc thẳng vào hai cực nguồn thì gần như không có dòng, vôn kế chỉ U0.", "Khi mắc thêm R, dòng điện là U1/R, và r gánh U0 − U1."],
  "Mắc vôn kế vào hai cực nguồn, đọc U0. Mắc R vào hai cực nguồn, vôn kế song song với R, đọc U1. I = U1/R, nên r = (U0 − U1)/I = R·(U0 − U1)/U1.")
P("T19", 3, "Đo khối lượng riêng của nút bấc",
  "Cho một lực kế, một cốc nước, một vật nặng chìm được (quả nặng) và một nút bấc nhẹ hơn nước. Trình bày cách xác định khối lượng riêng của nút bấc.",
  "D_bấc = D_nước·P_bấc/(P1 − P2)",
  ["Nút bấc tự nổi nên phải buộc vào quả nặng để nhấn chìm.", "Đo hai lần: quả nặng chìm còn bấc ở ngoài nước, và cả hai cùng chìm."],
  "Đo trọng lượng bấc P_bấc ngoài không khí. Treo quả nặng phía dưới, bấc phía trên vào lực kế: nhúng chỉ quả nặng trong nước, đọc P1. Nhúng chìm cả hai, đọc P2. Lực đẩy lên bấc: F = P1 − P2 = 10·D_nước·V. Vậy D_bấc = D_nước·P_bấc/(P1 − P2).")
P("T20", 1, "Đo thể tích vật không thấm nước",
  "Trình bày cách đo thể tích một hòn đá nhỏ có hình dạng bất kì bằng bình chia độ. Nếu hòn đá to không bỏ lọt bình thì làm thế nào?",
  "Đo mực nước trước và sau khi thả đá; vật to thì dùng bình tràn",
  ["Thể tích nước dâng lên bằng thể tích của vật."],
  "Đổ nước vào bình chia độ, đọc V1. Thả chìm hòn đá, đọc V2. V = V2 − V1. Với vật to: đổ đầy nước vào bình tràn, thả vật vào, hứng nước tràn ra rồi đổ vào bình chia độ để đo.")
