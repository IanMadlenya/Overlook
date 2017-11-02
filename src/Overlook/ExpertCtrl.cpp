#include "Overlook.h"

namespace Overlook {

void EvolutionGraph::Paint(Draw& w) {
	Size sz(GetSize());
	ImageDraw id(sz);
	id.DrawRect(sz, White());
	
	System& sys = GetSystem();
	ExpertSystem& esys = sys.GetExpertSystem();
	
	
	const Vector<AccuracyConf>& acc_list = esys.acc_list;
	
	double min = +DBL_MAX;
	double max = -DBL_MAX;
	
	int count = acc_list.GetCount();
	for(int j = 0; j < count; j++) {
		double d = acc_list[j].test_mult_valuehourfactor;
		if (d > max) max = d;
		if (d < min) min = d;
	}
	
	if (count >= 2 && max > min) {
		double diff = max - min;
		double xstep = (double)sz.cx / (count - 1);
		
		polyline.SetCount(count);
		for(int j = 0; j < count; j++) {
			double v = acc_list[j].test_mult_valuehourfactor;
			int y = (int)(sz.cy - (v - min) / diff * sz.cy);
			int x = (int)(j * xstep);
			polyline[j] = Point(x, y);
		}
		id.DrawPolyline(polyline, 1, Color(193, 255, 255));
		for(int j = 0; j < polyline.GetCount(); j++) {
			const Point& p = polyline[j];
			id.DrawRect(p.x-1, p.y-1, 3, 3, Blue());
		}
	}
	
	id.DrawText(3, 3, IntStr(count), Monospace(15), Black());
	
	w.DrawImage(0, 0, id);
}





ExpertOptimizerCtrl::ExpertOptimizerCtrl() {
	Add(vsplit.SizePos());
	
	vsplit << graph << hsplit;
	vsplit.Vert();
	vsplit.SetPos(3333);
	
	hsplit << pop << unit;
	hsplit.Horz();
	hsplit.SetPos(8000);
	
	pop.AddColumn("id");
	pop.AddColumn("symbol");
	pop.AddColumn("label_id");
	pop.AddColumn("period");
	pop.AddColumn("ext");
	pop.AddColumn("label");
	pop.AddColumn("ext_dir");
	pop.AddColumn("mask_valuefactor");
	pop.AddColumn("mask_valuehourfactor");
	pop.AddColumn("mask_hourtotal");
	pop.AddColumn("succ_valuefactor");
	pop.AddColumn("succ_valuehourfactor");
	pop.AddColumn("succ_hourtotal");
	pop.AddColumn("mult_valuefactor");
	pop.AddColumn("mult_valuehourfactor");
	pop.AddColumn("mult_hourtotal");
	pop.AddColumn("is_processed");
	
	unit.AddColumn("Key");
	unit.AddColumn("Value");
}

void ExpertOptimizerCtrl::Data() {
	System& sys = GetSystem();
	ExpertSystem& esys = sys.GetExpertSystem();
	
	const Vector<AccuracyConf>& acc_list = esys.acc_list;
	
	graph.Refresh();
	
	for(int i = 0; i < acc_list.GetCount(); i++) {
		const AccuracyConf& conf = acc_list[i];
		
		pop.Set(i, 0, conf.id);
		pop.Set(i, 1, conf.symbol);
		pop.Set(i, 2, conf.label_id);
		pop.Set(i, 3, conf.period);
		pop.Set(i, 4, conf.ext);
		pop.Set(i, 5, conf.label ? "true" : "false");
		pop.Set(i, 6, conf.ext_dir ? "true" : "false");
		pop.Set(i, 7, Format("%2!,n", conf.test_mask_valuefactor));
		pop.Set(i, 8, Format("%4!,n", conf.test_mask_valuehourfactor));
		pop.Set(i, 9, Format("%2!,n", conf.test_mask_hourtotal));
		pop.Set(i, 10, Format("%2!,n", conf.test_succ_valuefactor));
		pop.Set(i, 11, Format("%4!,n", conf.test_succ_valuehourfactor));
		pop.Set(i, 12, Format("%2!,n", conf.test_succ_hourtotal));
		pop.Set(i, 13, Format("%2!,n", conf.test_mult_valuefactor));
		pop.Set(i, 14, Format("%4!,n", conf.test_mult_valuehourfactor));
		pop.Set(i, 15, Format("%2!,n", conf.test_mult_hourtotal));
		pop.Set(i, 16, conf.is_processed ? "true" : "false");
	}
	
	int cursor = pop.GetCursor();
	if (cursor >= 0 && cursor < acc_list.GetCount()) {
		const AccuracyConf& conf = acc_list[cursor];
		ArrayCtrlPrinter printer(unit);
		conf.Print(printer);
	}
	else unit.Clear();
	
}






ExpertRealCtrl::ExpertRealCtrl() {
	Add(refresh_now.LeftPos(2,96).TopPos(2,26));
	Add(last_update.LeftPos(100,200).TopPos(2,26));
	Add(hsplit.HSizePos().VSizePos(30));
	
	refresh_now.SetLabel("Refresh now");
	refresh_now <<= THISBACK(RefreshNow);
	last_update.SetLabel("Last update:");
	
	hsplit << pop << unit;
	hsplit.Horz();
	hsplit.SetPos(8000);
	
	pop.AddColumn("id");
	pop.AddColumn("symbol");
	pop.AddColumn("label_id");
	pop.AddColumn("period");
	pop.AddColumn("ext");
	pop.AddColumn("label");
	pop.AddColumn("ext_dir");
	pop.AddColumn("mask_valuefactor");
	pop.AddColumn("mask_valuehourfactor");
	pop.AddColumn("mask_hourtotal");
	pop.AddColumn("succ_valuefactor");
	pop.AddColumn("succ_valuehourfactor");
	pop.AddColumn("succ_hourtotal");
	pop.AddColumn("mult_valuefactor");
	pop.AddColumn("mult_valuehourfactor");
	pop.AddColumn("mult_hourtotal");
	pop.AddColumn("is_processed");
	
	unit.AddColumn("Key");
	unit.AddColumn("Value");
}

void ExpertRealCtrl::RefreshNow() {
	System& sys = GetSystem();
	ExpertSystem& esys = sys.GetExpertSystem();
	esys.forced_update = true;
}

void ExpertRealCtrl::Data() {
	System& sys = GetSystem();
	ExpertSystem& esys = sys.GetExpertSystem();
	
	last_update.SetLabel("Last update: " + Format("%", esys.last_update));
	const Vector<AccuracyConf>& acc_list = esys.acc_list;
	
	for(int i = 0; i < esys.used_conf.GetCount(); i++) {
		const AccuracyConf& conf = acc_list[esys.used_conf[i]];
		
		pop.Set(i, 0, conf.id);
		pop.Set(i, 1, conf.symbol);
		pop.Set(i, 2, conf.label_id);
		pop.Set(i, 3, conf.period);
		pop.Set(i, 4, conf.ext);
		pop.Set(i, 5, conf.label ? "true" : "false");
		pop.Set(i, 6, conf.ext_dir ? "true" : "false");
		pop.Set(i, 7, Format("%2!,n", conf.test_mask_valuefactor));
		pop.Set(i, 8, Format("%4!,n", conf.test_mask_valuehourfactor));
		pop.Set(i, 9, Format("%2!,n", conf.test_mask_hourtotal));
		pop.Set(i, 10, Format("%2!,n", conf.test_succ_valuefactor));
		pop.Set(i, 11, Format("%4!,n", conf.test_succ_valuehourfactor));
		pop.Set(i, 12, Format("%2!,n", conf.test_succ_hourtotal));
		pop.Set(i, 13, Format("%2!,n", conf.test_mult_valuefactor));
		pop.Set(i, 14, Format("%4!,n", conf.test_mult_valuehourfactor));
		pop.Set(i, 15, Format("%2!,n", conf.test_mult_hourtotal));
		pop.Set(i, 16, conf.is_processed ? "true" : "false");
	}
	pop.SetCount(acc_list.GetCount());
	
	int cursor = pop.GetCursor();
	if (cursor >= 0 && cursor < esys.used_conf.GetCount()) {
		const AccuracyConf& conf = acc_list[esys.used_conf[cursor]];
		ArrayCtrlPrinter printer(unit);
		conf.Print(printer);
	}
	else unit.Clear();
}

}
