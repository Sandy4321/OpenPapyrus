// PPPOSPROTOCOL.CPP
// Copyright (c) A.Sobolev 2016, 2017
//
#include <pp.h>
#pragma hdrstop

/*

pap

source:
	system:
	version:
	name:
	uuid:
	code:

destination:
	system:
	version:
	name:
	uuid:
	code:
*/

/*
	query-csession [id] [date] [daterange] [last] [current]
		tokId, tokCode, tokPeriod, tokTime
	query-refs [objtype]
		tokObjType
*/
class ACS_PAPYRUS_APN : public PPAsyncCashSession {
public:
	SLAPI  ACS_PAPYRUS_APN(PPID n, PPID parent) : PPAsyncCashSession(n), ParentNodeID(parent)
	{
		PPAsyncCashNode acn;
		if(GetNodeData(&acn) > 0) {
			acn.GetLogNumList(LogNumList);
			ExpPath = acn.ExpPaths;
			ImpPath = acn.ImpFiles;
		}
		StatID = 0;
	}
	SLAPI ~ACS_PAPYRUS_APN()
	{
	}
	virtual int SLAPI ExportData(int updOnly);
	virtual int SLAPI GetSessionData(int * pSessCount, int * pIsForwardSess, DateRange * pPrd = 0)
	{
		IL.freeAll();

		int    ok = -1;
		int    is_forward = 0;
		int    sess_count = 0;
		DateRange period;
		period.SetZero();
		period.low = MAXDATE;
		SString imp_path;
		SString temp_buf;
		StringSet ss_paths(';', ImpPath);
		for(uint i = 0, set_no = 0; ss_paths.get(&i, imp_path); set_no++) {
			imp_path.SetLastSlash();
			(temp_buf = imp_path).CatChar('*').Dot().Cat("xml");
			SDirEntry de;
            for(SDirec sd(temp_buf, 0); sd.Next(&de) > 0;) {
                if(de.IsFile()) {
					(temp_buf = imp_path).Cat(de.FileName);
					int   pr = Pp.SaxParseFile(temp_buf);
					if(pr > 0) {
						const PPPosProtocol::ReadBlock & r_rb = Pp.GetReadBlock();
						uint local_sess_count = 0;
						for(uint j = 0; ok < 0 && j < r_rb.RefList.getCount(); j++) {
							const PPPosProtocol::ObjBlockRef & r_ref = r_rb.RefList.at(j);
							if(r_ref.Type == PPPosProtocol::obCSession) {
								const PPPosProtocol::CSessionBlock & r_sess_blk = r_rb.CSessBlkList.at(r_ref.P);
								period.AdjustToDate(r_sess_blk.Dtm.d);
								local_sess_count++;
							}
						}
                        if(local_sess_count) {
							ImportBlock * p_new_ib = new ImportBlock;
							THROW_MEM(p_new_ib);
							p_new_ib->Rb.Copy(r_rb);
							p_new_ib->SrcFileName = temp_buf;
							THROW_SL(IL.insert(p_new_ib));
							sess_count += (int)local_sess_count;
                        }
					}
					Pp.DestroyReadBlock();
                }
            }
		}
		if(sess_count) {
			THROW(CreateTables());
		}
		CATCHZOK
		ASSIGN_PTR(pIsForwardSess, is_forward);
		ASSIGN_PTR(pSessCount, sess_count);
		ASSIGN_PTR(pPrd, period);
		return ok;
	}
	virtual int SLAPI ImportSession(int sessN)
	{
		int    ok = -1;
        int    local_n = 0;
        for(uint i = 0; ok < 0 && i < IL.getCount(); i++) {
			const ImportBlock * p_ib = IL.at(i);
			if(p_ib) {
				for(uint j = 0; ok < 0 && j < p_ib->Rb.RefList.getCount(); j++) {
                    const PPPosProtocol::ObjBlockRef & r_ref = p_ib->Rb.RefList.at(j);
					if(r_ref.Type == PPPosProtocol::obCSession) {
						if(local_n == sessN) {
							THROW(ProcessImportSession(p_ib->Rb, j));
							ok = 1;
						}
						else
							local_n++;
					}
				}
			}
        }
		CATCHZOK
		return ok;
	}
	virtual int SLAPI FinishImportSession(PPIDArray * pList)
	{
		return -1;
	}
private:
	int    ProcessImportSession(const PPPosProtocol::ReadBlock & rRb, uint refP)
	{
		int    ok = 1;
		int    type = 0;
		PPID   src_ar_id = 0;
		SString temp_buf;
        const PPPosProtocol::CSessionBlock * p_cb = (const PPPosProtocol::CSessionBlock *)rRb.GetItem(refP, &type);
        const uint rc = rRb.RefList.getCount();
        THROW(type == PPPosProtocol::obCSession);
        {
        	PPTransaction tra(1);
        	THROW(tra);
			for(uint cc_refi = 0; cc_refi < rc; cc_refi++) {
				const PPPosProtocol::ObjBlockRef & r_cc_ref = rRb.RefList.at(cc_refi);
				const long pos_n = 1; // @stub // ������������� ����� �����
				if(r_cc_ref.Type == PPPosProtocol::obCCheck) {
					int    cc_type = 0;
					const PPPosProtocol::CCheckBlock * p_ccb = (const PPPosProtocol::CCheckBlock *)rRb.GetItem(cc_refi, &cc_type);
					THROW(cc_type == PPPosProtocol::obCCheck);
					if(p_ccb->CSessionBlkP == refP) {
						int    ccr = 0;
						PPID   cc_id = 0;
						PPID   cashier_id = 0;
						PPID   sc_id = 0; // �� ������������ �����, ����������� � ����
						LDATETIME cc_dtm = p_ccb->Dtm;
						double cc_amount = p_ccb->Amount;
						double cc_discount = p_ccb->Discount;
						long   cc_flags = 0;
						if(p_ccb->SaCcFlags & CCHKF_RETURN)
							cc_flags |= CCHKF_RETURN;
						if(p_ccb->SCardBlkP) {
							int    sc_type = 0;
							SCardTbl::Rec sc_rec;
							const PPPosProtocol::SCardBlock * p_scb = (const PPPosProtocol::SCardBlock *)rRb.GetItem(p_ccb->SCardBlkP, &sc_type);
							assert(sc_type == PPPosProtocol::obSCard);
							if(p_scb->NativeID) {
								sc_id = p_scb->NativeID;
							}
							else {
								rRb.GetS(p_scb->CodeP, temp_buf);
								temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
								if(ScObj.SearchCode(0, temp_buf, &sc_rec) > 0) {
									sc_id = sc_rec.ID;
								}
								else {
									; // @log
								}
							}
						}
						THROW(ccr = AddTempCheck(&cc_id, p_cb->Code, cc_flags, pos_n, p_ccb->Code, cashier_id, sc_id, &cc_dtm, cc_amount, cc_discount));
						//
						for(uint cl_refi = 0; cl_refi < rc; cl_refi++) {
							const PPPosProtocol::ObjBlockRef & r_cl_ref = rRb.RefList.at(cl_refi);
							if(r_cl_ref.Type == PPPosProtocol::obCcLine) {
								int    cl_type = 0;
								const PPPosProtocol::CcLineBlock * p_clb = (const PPPosProtocol::CcLineBlock *)rRb.GetItem(cl_refi, &cl_type);
								THROW(cl_type == PPPosProtocol::obCcLine);
								if(p_clb->CCheckBlkP == cc_refi) {
									short  div_n = (short)p_clb->DivN;
									double qtty = fabs(p_clb->Qtty);
									double price = p_clb->Price;
									double dscnt = (p_clb->SumDiscount != 0.0 && qtty != 0.0) ? (p_clb->SumDiscount / qtty) : p_clb->Discount;
									PPID   goods_id = 0;
									if(p_clb->GoodsBlkP) {
										int    g_type = 0;
										const PPPosProtocol::GoodsBlock * p_gb = (const PPPosProtocol::GoodsBlock *)rRb.GetItem(p_clb->GoodsBlkP, &g_type);
										assert(g_type == PPPosProtocol::obGoods);
										if(p_gb->NativeID) {
											goods_id = p_gb->NativeID;
										}
										else {
											THROW(Pp.ResolveGoodsBlock(*p_gb, p_clb->GoodsBlkP, 1, 0, 0, src_ar_id, &goods_id));
										}
									}
									SetupTempCcLineRec(0, cc_id, p_cb->Code, cc_dtm.d, div_n, goods_id);
									SetTempCcLineValues(0, qtty, price, dscnt, 0 /*serial*/);
									THROW_DB(P_TmpCclTbl->insertRec());
								}
							}
						}
					}
				}
			}
			THROW(tra.Commit());
        }
        CATCHZOK
		return ok;
	}

	PPID   StatID;
	PPID   ParentNodeID;
	PPIDArray  LogNumList;
	PPIDArray  SessAry;
	SString    ExpPath;
	SString    ImpPath;
	PPObjGoods GObj;
	PPObjPerson PsnObj;
	PPObjSCard  ScObj;

	struct ImportBlock {
		SString SrcFileName;
		PPPosProtocol::ReadBlock Rb;
	};
	TSCollection <ImportBlock> IL;
	PPPosProtocol Pp; // ��������� PPPosProtocol ����������� ��� ������� ������
};

class CM_PAPYRUS : public PPCashMachine {
public:
	SLAPI  CM_PAPYRUS(PPID posNodeID) : PPCashMachine(posNodeID)
	{
	}
	PPAsyncCashSession * SLAPI AsyncInterface()
	{
		return new ACS_PAPYRUS_APN(NodeID, ParentNodeID);
	}
};

REGISTER_CMT(PAPYRUS,0,1);

//virtual
int SLAPI ACS_PAPYRUS_APN::ExportData(int updOnly)
{
	return Pp.ExportDataForPosNode(NodeID, updOnly, SinceDlsID);
}

int SLAPI PPPosProtocol::WriteSourceRoute(WriteBlock & rB)
{
	int    ok = 1;
	DbProvider * p_dict = CurDict;
	PPPosProtocol::RouteBlock rb;
	PPVersionInfo vi = DS.GetVersionInfo();
	vi.GetProductName(rb.System);
	vi.GetVersion().ToStr(rb.Version);
	if(p_dict) {
		p_dict->GetDbUUID(&rb.Uuid);
		p_dict->GetDbSymb(rb.Code);
	}
	THROW(WriteRouteInfo(rB, "source", rb));
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::SendQuery(const PPPosProtocol::QueryBlock & rQ)
{
	int    ok = 1;
	SString out_file_name;
	SString temp_buf;
	THROW(oneof3(rQ.Q, QueryBlock::qTest, QueryBlock::qRefs, QueryBlock::qCSession));
	THROW(SelectOutFileName(0, "query", out_file_name));
	{
		PPPosProtocol::WriteBlock wb;
		THROW(StartWriting(out_file_name, wb));
		{
			THROW(WriteSourceRoute(wb));
			{
				PPPosProtocol::RouteBlock rb;
				THROW(WriteRouteInfo(wb, "destination", rb));
			}
		}
		{
			if(rQ.Q == QueryBlock::qCSession) {
				SXml::WNode w_s(wb.P_Xw, "query-csession");
				if(rQ.Flags & QueryBlock::fCSessCurrent) {
					w_s.PutInner("current", 0);
				}
				else if(rQ.Flags & QueryBlock::fCSessLast) {
					w_s.PutInner("last", 0);
				}
				else if(!rQ.Period.IsZero()) {
					THROW(checkdate(rQ.Period.low, 1) && checkdate(rQ.Period.upp, 1));
					temp_buf = 0;
					if(rQ.Period.low)
						temp_buf.Cat(rQ.Period.low, DATF_ISO8601|DATF_CENTURY);
					temp_buf.Dot().Dot();
					if(rQ.Period.upp)
						temp_buf.Cat(rQ.Period.upp, DATF_ISO8601|DATF_CENTURY);
					w_s.PutInner("period", temp_buf);
				}
				else if(rQ.CSess) {
					(temp_buf = 0).Cat(rQ.CSess);
					w_s.PutInner((rQ.Flags & QueryBlock::fCSessN) ? "code" : "id", temp_buf);
				}
			}
			else if(rQ.Q == QueryBlock::qRefs) {
				SXml::WNode w_s(wb.P_Xw, "query-refs");
			}
			else if(rQ.Q == QueryBlock::qTest) {
				SXml::WNode w_s(wb.P_Xw, "query-test");
			}
		}
		FinishWriting(wb);
	}
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::ExportDataForPosNode(PPID nodeID, int updOnly, PPID sinceDlsID)
{
	int    ok = 1;
	PPIDArray qk_list; // ������ ��������������� ����� ���������, ������� ������ �����������
	PPIDArray used_qk_list; // ������ ��������������� ����� ���������, ������� �����������
	SString out_file_name;
	SString fmt_buf, msg_buf;
	SString temp_buf;
	PPPosProtocol::WriteBlock wb;
	PPObjQuotKind qk_obj;
	PPObjGoods goods_obj;

	PPAsyncCashNode cn_data;
	//THROW(GetNodeData(&cn_data) > 0);
	PPObjCashNode cnobj;
	THROW(cnobj.GetAsync(nodeID, &cn_data) > 0);
	/*
	if(r > 0) {
		CnFlags = pData->Flags;
		CnExtFlags = pData->ExtFlags;
	}
	return r;
	*/

	PPWait(1);
	THROW(SelectOutFileName(0, "refs", out_file_name));
	{
		DeviceLoadingStat dls;
		PPID   stat_id = 0;
		dls.StartLoading(&stat_id, dvctCashs, nodeID, 1);
		THROW(StartWriting(out_file_name, wb));
		{
			PPQuotKind qk_rec;
			for(SEnum en = qk_obj.Enum(0); en.Next(&qk_rec) > 0;) {
				if(qk_rec.ID == PPQUOTK_BASE || (qk_rec.Flags & QUOTKF_RETAILED)) {
					qk_list.add(qk_rec.ID);
				}
			}
			qk_list.sortAndUndup();
		}
		THROW(WriteSourceRoute(wb));
		{
			PPPosProtocol::RouteBlock rb;
			rb.System = "Papyrus";
			THROW(WriteRouteInfo(wb, "destination", rb));
		}
		/*
		{
			SXml::WNode n_scope(wb.P_Xw, "destination");
		}
		*/
		{
			SXml::WNode n_scope(wb.P_Xw, "refs");
			{
				long   acgi_flags = ACGIF_ALLCODESPERITER;
				PPQuotArray qlist;
				if(updOnly)
					acgi_flags |= ACGIF_UPDATEDONLY;
				AsyncCashGoodsIterator acgi(nodeID, acgi_flags, sinceDlsID, &dls);
				if(cn_data.Flags & CASHF_EXPGOODSGROUPS) {
					AsyncCashGoodsGroupInfo acggi_info;
					AsyncCashGoodsGroupIterator * p_group_iter = acgi.GetGroupIterator();
					if(p_group_iter) {
						while(p_group_iter->Next(&acggi_info) > 0) {
							qlist.clear();
							goods_obj.GetQuotList(acggi_info.ID, cn_data.LocID, qlist);
							{
								uint qp = qlist.getCount();
								if(qp) do {
									PPQuot & r_q = qlist.at(--qp);
									if(!qk_list.bsearch(r_q.Kind))
										qlist.atFree(qp);
									else
										used_qk_list.addUnique(r_q.Kind);
								} while(qp);
							}
							THROW(WriteGoodsGroupInfo(wb, "goodsgroup", acggi_info, &qlist));
						}
					}
				}
				{
					// ������
					AsyncCashGoodsInfo acgi_item;
					while(acgi.Next(&acgi_item) > 0) {
						qlist.clear();
						goods_obj.GetQuotList(acgi_item.ID, cn_data.LocID, qlist);
						{
							uint qp = qlist.getCount();
							if(qp) do {
								PPQuot & r_q = qlist.at(--qp);
								if(!qk_list.bsearch(r_q.Kind))
									qlist.atFree(qp);
								else
									used_qk_list.addUnique(r_q.Kind);
							} while(qp);
						}
						THROW(WriteGoodsInfo(wb, "ware", acgi_item, &qlist));
						PPWaitPercent(acgi.GetIterCounter());
					}
				}
			}
			{
				//
				// �����
				//
				LAssocArray scard_quot_list;
				PPObjSCardSeries scs_obj;
				PPSCardSeries scs_rec;
				AsyncCashSCardsIterator acci(nodeID, updOnly, &dls, sinceDlsID);
				for(PPID ser_id = 0, idx = 1; scs_obj.EnumItems(&ser_id, &scs_rec) > 0;) {
					const int scs_type = scs_rec.GetType();
					//if(scs_type == scstDiscount || (scs_type == scstCredit && CrdCardAsDsc)) {
					{
						AsyncCashSCardInfo acci_item;
						PPSCardSerPacket scs_pack;
						if(scs_obj.GetPacket(ser_id, &scs_pack) > 0) {
							if(scs_rec.QuotKindID_s)
								THROW_SL(scard_quot_list.Add(scs_rec.ID, scs_rec.QuotKindID_s, 0));
							(msg_buf = fmt_buf).CatDiv(':', 2).Cat(scs_rec.Name);
							for(acci.Init(&scs_pack); acci.Next(&acci_item) > 0;) {
								THROW(WriteSCardInfo(wb, "card", acci_item));
								PPWaitPercent(acci.GetCounter(), msg_buf);
							}
						}
					}
				}
			}
			{
				//
				// ���� ���������
				//
				if(used_qk_list.getCount()) {
					PPQuotKind qk_rec;
					for(uint i = 0; i < used_qk_list.getCount(); i++) {
						if(qk_obj.Search(used_qk_list.get(i), &qk_rec) > 0) {
							THROW(WriteQuotKindInfo(wb, "quotekind", qk_rec));
						}
					}
				}
			}
		}
		FinishWriting(wb);
		if(stat_id)
			dls.FinishLoading(stat_id, 1, 1);
	}
	CATCHZOK
	PPWait(0);
	return ok;
}
//
//
//
SLAPI PPPosProtocol::WriteBlock::WriteBlock()
{
	P_Xw = 0;
	P_Xd = 0;
	P_Root = 0;
}

SLAPI PPPosProtocol::WriteBlock::~WriteBlock()
{
	Destroy();
}

void PPPosProtocol::WriteBlock::Destroy()
{
	ZDELETE(P_Root);
	ZDELETE(P_Xd);
	xmlFreeTextWriter(P_Xw);
	P_Xw = 0;
	UsedQkList.freeAll();
}

SLAPI PPPosProtocol::RouteBlock::RouteBlock()
{
	Uuid.SetZero();
}

void SLAPI PPPosProtocol::RouteBlock::Destroy()
{
	Uuid.SetZero();
	System = 0;
	Version = 0;
	Code = 0;
}

int SLAPI PPPosProtocol::RouteBlock::IsEmpty() const
{
	return BIN(Uuid.IsZero() && System.Empty() && Version.Empty() && Code.Empty());
}

int FASTCALL PPPosProtocol::RouteBlock::IsEqual(const RouteBlock & rS) const
{
	int    yes = 1;
	if(!Uuid.IsZero()) {
		if(!rS.Uuid.IsZero())
			yes = (Uuid == rS.Uuid);
		else
			yes = 0;
	}
	else if(!rS.Uuid.IsZero())
		yes = 0;
	else if(Code.NotEmpty()) {
		if(rS.Code.NotEmpty())
			yes = (Code == rS.Code);
		else
			yes = 0;
	}
	else if(rS.Code.NotEmpty())
		yes = 0;
	return yes;
}
//
//
//
SLAPI PPPosProtocol::ObjectBlock::ObjectBlock()
{
	Flags = 0;
	ID = 0;
	NativeID = 0;
	NameP = 0;
}

SLAPI PPPosProtocol::PosNodeBlock::PosNodeBlock() : ObjectBlock()
{
	CodeP = 0;
	CodeI = 0;
}

SLAPI PPPosProtocol::QuotKindBlock::QuotKindBlock() : ObjectBlock()
{
    CodeP = 0;
	Rank = 0;
	Reserve = 0;
    Period.SetZero();
	TimeRestriction.SetZero();
	AmountRestriction.Clear();
}

SLAPI PPPosProtocol::GoodsBlock::GoodsBlock() : ObjectBlock()
{
	ParentBlkP = 0;
	InnerId = 0;
	Price = 0.0;
}

SLAPI PPPosProtocol::GoodsGroupBlock::GoodsGroupBlock() : ObjectBlock()
{
	CodeP = 0;
	ParentBlkP = 0;
}

SLAPI PPPosProtocol::PersonBlock::PersonBlock() : ObjectBlock()
{
	CodeP = 0;
}

SLAPI PPPosProtocol::SCardBlock::SCardBlock() : ObjectBlock()
{
	CodeP = 0;
	OwnerBlkP = 0;
	Discount = 0.0;
}

SLAPI PPPosProtocol::CSessionBlock::CSessionBlock() : ObjectBlock()
{
	ID = 0;
	Code = 0;
	PosBlkP = 0;
	Dtm.SetZero();
}

SLAPI PPPosProtocol::CCheckBlock::CCheckBlock() : ObjectBlock()
{
	Code = 0;
	CcFlags = 0;
	SaCcFlags = 0;
	CTableN = 0;
	GuestCount = 0;
	CSessionBlkP = 0;
	AddrBlkP = 0;
	AgentBlkP = 0;
	Amount = 0.0;
	Discount = 0.0;
	Dtm.SetZero();
	CreationDtm.SetZero();
	SCardBlkP = 0;
	MemoP = 0;
}

SLAPI PPPosProtocol::CcLineBlock::CcLineBlock() : ObjectBlock()
{
	CcID = 0;
	RByCheck = 0; // (id)
	CclFlags = 0;
	DivN = 0;
	Queue = 0;
	GoodsBlkP = 0;
	Qtty = 0.0;
	Price = 0.0;
	Discount = 0.0;
	SumDiscount = 0.0;
	Amount = 0.0;
	CCheckBlkP = 0;
	SerialP = 0;
	EgaisMarkP = 0;
}

SLAPI PPPosProtocol::QueryBlock::QueryBlock()
{
	Init(qUnkn);
}

void FASTCALL PPPosProtocol::QueryBlock::Init(int q)
{
	THISZERO();
	Q = oneof3(q, qTest, qCSession, qRefs) ? q : qUnkn;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryCSessionLast()
{
	Init(qCSession);
	Flags = fCSessLast;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryCSessionCurrent()
{
	Init(qCSession);
	Flags = fCSessCurrent;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryCSessionByID(PPID sessID)
{
	Init(qCSession);
	CSess = sessID;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryCSessionByNo(long sessN)
{
	Init(qCSession);
	Flags = fCSessN;
	CSess = sessN;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryCSessionByDate(const DateRange & rPeriod)
{
	Init(qCSession);
	Period = rPeriod;
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryRefs()
{
	Init(qRefs);
}

void SLAPI PPPosProtocol::QueryBlock::SetQueryTest()
{
	Init(qTest);
}

SLAPI PPPosProtocol::QuotBlock::QuotBlock()
{
	THISZERO();
}

SLAPI PPPosProtocol::ParentBlock::ParentBlock()
{
	THISZERO();
}

SLAPI PPPosProtocol::GoodsCode::GoodsCode()
{
	THISZERO();
}

SLAPI PPPosProtocol::RouteObjectBlock::RouteObjectBlock() : ObjectBlock()
{
	Direction = 0;
	SystemP = 0;
	VersionP = 0;
	CodeP = 0;
	Uuid.SetZero();
}

SLAPI PPPosProtocol::ObjBlockRef::ObjBlockRef(int t, uint pos)
{
	Type = t;
	P = pos;
}

int  SLAPI PPPosProtocol::ReadBlock::CreateItem(int type, uint * pRefPos)
{
	int    ok = 1;
	switch(type) {
		case obGoods:       THROW(Helper_CreateItem(GoodsBlkList, type, pRefPos)); break;
		case obGoodsGroup:  THROW(Helper_CreateItem(GoodsGroupBlkList, type, pRefPos)); break;
		case obPerson:      THROW(Helper_CreateItem(PersonBlkList, type, pRefPos)); break;
		case obGoodsCode:   THROW(Helper_CreateItem(GoodsCodeList, type, pRefPos)); break;
		case obSCard:       THROW(Helper_CreateItem(SCardBlkList, type, pRefPos)); break;
		case obParent:      THROW(Helper_CreateItem(ParentBlkList, type, pRefPos)); break;
		case obQuotKind:    THROW(Helper_CreateItem(QkBlkList, type, pRefPos)); break;
		case obQuot:        THROW(Helper_CreateItem(QuotBlkList, type, pRefPos)); break;
		case obSource:      THROW(Helper_CreateItem(SrcBlkList, type, pRefPos)); break;
		case obDestination: THROW(Helper_CreateItem(DestBlkList, type, pRefPos)); break;
		case obCSession:    THROW(Helper_CreateItem(CSessBlkList, type, pRefPos)); break;
		case obCCheck:      THROW(Helper_CreateItem(CcBlkList, type, pRefPos)); break;
		case obCcLine:      THROW(Helper_CreateItem(CclBlkList, type, pRefPos)); break;
		case obPosNode:     THROW(Helper_CreateItem(PosBlkList, type, pRefPos)); break;
		case obQuery:       THROW(Helper_CreateItem(QueryList, type, pRefPos)); break;
		default:
			assert(0);
			CALLEXCEPT();
			break;
	}
	CATCHZOK
	return ok;
}

PPPosProtocol::ReadBlock & FASTCALL PPPosProtocol::ReadBlock::Copy(const PPPosProtocol::ReadBlock & rS)
{
	Destroy();
	SStrGroup::CopyS(rS);
	#define CPY_FLD(f) f = rS.f
	CPY_FLD(SrcBlkList);
	CPY_FLD(DestBlkList);
	CPY_FLD(GoodsBlkList);
	CPY_FLD(GoodsGroupBlkList);
	CPY_FLD(GoodsCodeList);
	CPY_FLD(QkBlkList);
	CPY_FLD(QuotBlkList);
	CPY_FLD(PersonBlkList);
	CPY_FLD(SCardBlkList);
	CPY_FLD(ParentBlkList);
	CPY_FLD(PosBlkList);
	CPY_FLD(CSessBlkList);
	CPY_FLD(CcBlkList);
	CPY_FLD(CclBlkList);
	CPY_FLD(QueryList);
	CPY_FLD(RefList);
	#undef CPY_FLD
	return *this;
}

void * SLAPI PPPosProtocol::ReadBlock::GetItem(uint refPos, int * pType) const
{
	void * p_ret = 0;
	int    type = 0;
	if(refPos < RefList.getCount()) {
		const ObjBlockRef & r_ref = RefList.at(refPos);
		type = r_ref.Type;
		switch(r_ref.Type) {
			case obGoods:       p_ret = &GoodsBlkList.at(r_ref.P); break;
			case obGoodsGroup:  p_ret = &GoodsGroupBlkList.at(r_ref.P); break;
			case obPerson:      p_ret = &PersonBlkList.at(r_ref.P); break;
			case obGoodsCode:   p_ret = &GoodsCodeList.at(r_ref.P); break;
			case obSCard:       p_ret = &SCardBlkList.at(r_ref.P); break;
			case obParent:      p_ret = &ParentBlkList.at(r_ref.P); break;
			case obQuotKind:    p_ret = &QkBlkList.at(r_ref.P); break;
			case obQuot:        p_ret = &QuotBlkList.at(r_ref.P); break;
			case obSource:      p_ret = &SrcBlkList.at(r_ref.P); break;
			case obDestination: p_ret = &DestBlkList.at(r_ref.P); break;
			case obCSession:    p_ret = &CSessBlkList.at(r_ref.P); break;
			case obCCheck:      p_ret = &CcBlkList.at(r_ref.P); break;
			case obCcLine:      p_ret = &CclBlkList.at(r_ref.P); break;
			case obPosNode:     p_ret = &PosBlkList.at(r_ref.P); break;
			case obQuery:       p_ret = &QueryList.at(r_ref.P); break;
			default:
				assert(0);
				CALLEXCEPT();
				break;
		}
	}
	CATCH
		p_ret = 0;
	ENDCATCH
	ASSIGN_PTR(pType, type);
	return p_ret;
}

int SLAPI PPPosProtocol::ReadBlock::SearchRef(int type, uint pos, uint * pRefPos) const
{
	int    ok = 0;
	uint   ref_pos = 0;
	for(uint i = 0; !ok && i < RefList.getCount(); i++) {
		const ObjBlockRef & r_ref = RefList.at(i);
		if(r_ref.Type == type && r_ref.P == pos) {
			ref_pos = i;
			ok = 1;
		}
	}
	ASSIGN_PTR(pRefPos, ref_pos);
	return ok;
}

int SLAPI PPPosProtocol::ReadBlock::SearchAnalogRef_QuotKind(const PPPosProtocol::QuotKindBlock & rBlk, uint exclPos, uint * pRefPos) const
{
	int    ok = 0;
	uint   ref_pos = 0;
	SString code_buf;
	GetS(rBlk.CodeP, code_buf);
	if(rBlk.ID || code_buf.NotEmpty()) {
		SString temp_buf;
		for(uint i = 0; !ok && i < RefList.getCount(); i++) {
			if((!exclPos || i != exclPos) && RefList.at(i).Type == obQuotKind) {
				int   test_type = 0;
				const QuotKindBlock * p_item = (const QuotKindBlock *)GetItem(i, &test_type);
				assert(test_type == obQuotKind);
				if(rBlk.ID) {
					if(p_item->ID == rBlk.ID) {
						if(code_buf.NotEmpty()) {
							if(GetS(p_item->CodeP, temp_buf) && code_buf == temp_buf) {
								ref_pos = i;
								ok = 1;
							}
						}
						else if(p_item->CodeP == 0) {
							ref_pos = i;
							ok = 1;
						}
					}
				}
				else if(code_buf.NotEmpty() && GetS(p_item->CodeP, temp_buf) && code_buf == temp_buf) {
					ref_pos = i;
					ok = 1;
				}
			}
		}
	}
	ASSIGN_PTR(pRefPos, ref_pos);
	return ok;
}

const PPPosProtocol::QuotKindBlock * FASTCALL PPPosProtocol::ReadBlock::SearchAnalog_QuotKind(const PPPosProtocol::QuotKindBlock & rBlk) const
{
	const QuotKindBlock * p_ret = 0;
	if(rBlk.ID) {
		for(uint i = 0; !p_ret && i < QkBlkList.getCount(); i++) {
			const QuotKindBlock & r_item = QkBlkList.at(i);
			if(r_item.NativeID && r_item.ID == rBlk.ID) {
				p_ret = &r_item;
			}
		}
	}
	return p_ret;
}
//
// Descr: ������� �������� ������ ����� ���������� rBlk � ������������������
//   NativeID.
// Returns:
//   ��������� �� ��������� ����-������
//   ���� ����� �������� �����������, �� ���������� 0
//
const PPPosProtocol::PersonBlock * FASTCALL PPPosProtocol::ReadBlock::SearchAnalog_Person(const PPPosProtocol::PersonBlock & rBlk) const
{
	const PersonBlock * p_ret = 0;
	if(rBlk.ID) {
		for(uint i = 0; !p_ret && i < PersonBlkList.getCount(); i++) {
			const PersonBlock & r_item = PersonBlkList.at(i);
			if(r_item.NativeID && r_item.ID == rBlk.ID) {
				p_ret = &r_item;
			}
		}
	}
	return p_ret;
}

SLAPI PPPosProtocol::PPPosProtocol()
{
}

SLAPI PPPosProtocol::~PPPosProtocol()
{
}

int SLAPI PPPosProtocol::ReadBlock::GetRouteItem(const RouteObjectBlock & rO, RouteBlock & rR) const
{
	int    ok = 1;
	rR.Destroy();
	rR.Uuid = rO.Uuid;
	GetS(rO.CodeP, rR.Code);
	GetS(rO.SystemP, rR.System);
	GetS(rO.VersionP, rR.Version);
	return ok;
}

const SString & FASTCALL PPPosProtocol::EncText(const char * pS)
{
	EncBuf = pS;
	PROFILE(XMLReplaceSpecSymb(EncBuf, "&<>\'"));
	return EncBuf.Transf(CTRANSF_INNER_TO_UTF8);
}

int SLAPI PPPosProtocol::WritePosNode(WriteBlock & rB, const char * pScopeXmlTag, PPCashNode & rInfo)
{
    int    ok = 1;
	SString temp_buf;
	{
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
        w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.ID));
        w_s.PutInnerSkipEmpty("code", EncText(rInfo.Symb));
        w_s.PutInnerSkipEmpty("name", EncText(rInfo.Name));
	}
    return ok;
}

int SLAPI PPPosProtocol::WriteCSession(WriteBlock & rB, const char * pScopeXmlTag, const CSessionTbl::Rec & rInfo)
{
	int    ok = 1;
	PPID   src_ar_id = 0; // ������ �������������� �����, ��������������� ��������� ������
	LDATETIME dtm;
	SString temp_buf;
	{
		/*
	struct Rec {
		int32  ID;
		int32  SuperSessID;
		int32  CashNodeID;
		int32  CashNumber;
		int32  SessNumber;
		LDATE  Dt;
		LTIME  Tm;
		int16  Incomplete;
		int16  Temporary;
		double Amount;
		double Discount;
		double AggrAmount;
		double AggrRest;
		double WrOffAmount;
		double WrOffCost;
		double Income;
		double BnkAmount;
		double CSCardAmount;
	} data;
		*/
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.ID));
		w_s.PutInner("code", (temp_buf = 0).Cat(rInfo.SessNumber));
		if(rInfo.CashNodeID) {
			PPObjCashNode cn_obj;
			PPCashNode cn_rec;
			if(cn_obj.Search(rInfo.CashNodeID, &cn_rec) > 0) {
				THROW(WritePosNode(rB, "pos", cn_rec));
			}
		}
		{
			dtm.Set(rInfo.Dt, rInfo.Tm);
			(temp_buf = 0).Cat(dtm, DATF_ISO8601|DATF_CENTURY, 0);
			w_s.PutInner("time", EncText(temp_buf));
		}
		{
			PPIDArray cc_list;
			ArGoodsCodeArray ar_code_list;
            CCheckCore * p_cc = ScObj.P_CcTbl;
            if(p_cc) {
				CCheckPacket cc_pack;
                THROW(p_cc->GetListBySess(rInfo.ID, 0, cc_list));
				for(uint i = 0; i < cc_list.getCount(); i++) {
					const PPID cc_id = cc_list.get(i);
					cc_pack.Init();
					if(p_cc->LoadPacket(cc_id, 0, &cc_pack) > 0) {
						const double cc_amount = MONEYTOLDBL(cc_pack.Rec.Amount);
						const double cc_discount = MONEYTOLDBL(cc_pack.Rec.Discount);
						SXml::WNode w_cc(rB.P_Xw, "cc");
                        w_cc.PutInner("id",   (temp_buf = 0).Cat(cc_pack.Rec.ID));
                        w_cc.PutInner("code", (temp_buf = 0).Cat(cc_pack.Rec.Code));
						{
							dtm.Set(cc_pack.Rec.Dt, cc_pack.Rec.Tm);
							(temp_buf = 0).Cat(dtm, DATF_ISO8601|DATF_CENTURY, 0);
							w_cc.PutInner("time", EncText(temp_buf));
						}
						if(cc_pack.Rec.Flags) {
							(temp_buf = 0).CatHex(cc_pack.Rec.Flags);
							w_cc.PutInner("flags", EncText(temp_buf));
						}
						if(cc_pack.Rec.Flags & CCHKF_RETURN) {
							w_cc.PutInner("return", "");
						}
						(temp_buf = 0).Cat(cc_amount, MKSFMTD(0, 2, NMBF_NOTRAILZ));
						w_cc.PutInner("amount", temp_buf);
						if(cc_discount != 0.0) {
							(temp_buf = 0).Cat(cc_discount, MKSFMTD(0, 2, NMBF_NOTRAILZ));
							w_cc.PutInner("discount", temp_buf);
						}
						if(cc_pack.Rec.SCardID) {
							SCardTbl::Rec sc_rec;
							if(ScObj.Search(cc_pack.Rec.SCardID, &sc_rec) > 0) {
								SXml::WNode w_sc(rB.P_Xw, "card");
								w_sc.PutInner("code", EncText(temp_buf = sc_rec.Code));
							}
						}
						for(uint ln_idx = 0; ln_idx < cc_pack.GetCount(); ln_idx++) {
							const CCheckLineTbl::Rec & r_item = cc_pack.GetLine(ln_idx);

							const double item_qtty  = fabs(r_item.Quantity);
							const double item_price = intmnytodbl(r_item.Price);
							const double item_discount = r_item.Dscnt;
							const double item_amount   = item_price * item_qtty;
							const double item_sum_discount = item_discount * item_qtty;

							SXml::WNode w_ccl(rB.P_Xw, "ccl");
                            w_ccl.PutInner("id", (temp_buf = 0).Cat(r_item.RByCheck));
                            if(r_item.GoodsID) {
								Goods2Tbl::Rec goods_rec;
								if(GObj.Search(r_item.GoodsID, &goods_rec) > 0) {
									SXml::WNode w_w(rB.P_Xw, "ware");
									GObj.P_Tbl->ReadArCodesByAr(goods_rec.ID, src_ar_id, &ar_code_list);
									if(ar_code_list.getCount()) {
										w_w.PutInner("id", EncText((temp_buf = 0).Cat(ar_code_list.at(0).Code)));
									}
									w_w.PutInner("innerid", (temp_buf = 0).Cat(goods_rec.ID));
                                    GObj.GetSingleBarcode(r_item.GoodsID, temp_buf);
                                    if(temp_buf.NotEmptyS()) {
										w_w.PutInner("code", EncText(temp_buf));
                                    }
								}
                            }
                            w_ccl.PutInner("qtty", (temp_buf = 0).Cat(item_qtty, MKSFMTD(0, 6, NMBF_NOTRAILZ)));
                            w_ccl.PutInner("price", (temp_buf = 0).Cat(item_price, MKSFMTD(0, 5, NMBF_NOTRAILZ)));
                            if(item_discount != 0.0)
								w_ccl.PutInner("discount", (temp_buf = 0).Cat(item_discount, MKSFMTD(0, 5, NMBF_NOTRAILZ)));
							w_ccl.PutInner("amount", (temp_buf = 0).Cat(item_amount, MKSFMTD(0, 5, NMBF_NOTRAILZ)));
							if(item_sum_discount != 0.0)
								w_ccl.PutInner("sumdiscount", (temp_buf = 0).Cat(item_sum_discount, MKSFMTD(0, 5, NMBF_NOTRAILZ)));
						}
					}
				}
            }
		}
	}
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::WriteGoodsGroupInfo(WriteBlock & rB, const char * pScopeXmlTag, const AsyncCashGoodsGroupInfo & rInfo, const PPQuotArray * pQList)
{
	int    ok = 1;
	SString temp_buf;
	{
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.ID));
		w_s.PutInner("name", EncText(rInfo.Name));
		if(rInfo.Code[0]) {
			temp_buf = rInfo.Code;
			w_s.PutInner("code", EncText(temp_buf));
		}
		if(rInfo.ParentID) {
			SXml::WNode w_p(rB.P_Xw, "parent");
			w_p.PutInner("id", (temp_buf = 0).Cat(rInfo.ParentID));
		}
		if(pQList && pQList->getCount()) {
			for(uint i = 0; i < pQList->getCount(); i++) {
				WriteQuotInfo(rB, "quote", PPOBJ_GOODSGROUP, pQList->at(i));
			}
		}
	}
	return ok;
}

int SLAPI PPPosProtocol::WriteRouteInfo(WriteBlock & rB, const char * pScopeXmlTag, PPPosProtocol::RouteBlock & rInfo)
{
	int    ok = 1;
	if(!rInfo.IsEmpty()) {
		SString temp_buf;
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		PPVersionInfo vi = DS.GetVersionInfo();
		vi.GetProductName(temp_buf);
		w_s.PutInnerSkipEmpty("system", EncText(temp_buf = rInfo.System));
		w_s.PutInnerSkipEmpty("version", EncText(temp_buf = rInfo.Version));
		if(!rInfo.Uuid.IsZero()) {
			rInfo.Uuid.ToStr(S_GUID::fmtIDL, temp_buf);
			w_s.PutInner("uuid", EncText(temp_buf));
		}
		w_s.PutInnerSkipEmpty("code", EncText(temp_buf = rInfo.Code));
	}
	else
		ok = -1;
	return ok;
}

int SLAPI PPPosProtocol::WriteGoodsInfo(WriteBlock & rB, const char * pScopeXmlTag, const AsyncCashGoodsInfo & rInfo, const PPQuotArray * pQList)
{
	int    ok = 1;
	SString temp_buf;
	{
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.ID));
		w_s.PutInner("name", EncText(rInfo.Name));
		if(rInfo.P_CodeList && rInfo.P_CodeList->getCount()) {
			for(uint i = 0; i < rInfo.P_CodeList->getCount(); i++) {
				const BarcodeTbl::Rec & r_bc_rec = rInfo.P_CodeList->at(i);
				w_s.PutInner("code", EncText(r_bc_rec.Code));
			}
		}
		if(rInfo.ParentID) {
			SXml::WNode w_p(rB.P_Xw, "parent");
			w_p.PutInner("id", (temp_buf = 0).Cat(rInfo.ParentID));
		}
		if(rInfo.Price > 0.0)
			w_s.PutInner("price", (temp_buf = 0).Cat(rInfo.Price, MKSFMTD(0, 5, NMBF_NOTRAILZ)));
		if(rInfo.Rest > 0.0)
			w_s.PutInner("rest", (temp_buf = 0).Cat(rInfo.Rest, MKSFMTD(0, 6, NMBF_NOTRAILZ)));
		if(pQList && pQList->getCount()) {
			for(uint i = 0; i < pQList->getCount(); i++) {
				WriteQuotInfo(rB, "quote", PPOBJ_GOODS, pQList->at(i));
			}
		}
	}
	return ok;
}

int SLAPI PPPosProtocol::WriteQuotKindInfo(WriteBlock & rB, const char * pScopeXmlTag, const PPQuotKind & rInfo)
{
    int    ok = 1;
    SString temp_buf;
	TimeRange tr;
    {
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.ID));
		w_s.PutInner("name", EncText(rInfo.Name));
		w_s.PutInnerSkipEmpty("code", EncText(rInfo.Symb));
		w_s.PutInner("rank", (temp_buf = 0).Cat(rInfo.Rank));
		if(!rInfo.Period.IsZero() || rInfo.HasWeekDayRestriction() || rInfo.GetTimeRange(tr) > 0 || !rInfo.AmtRestr.IsZero()) {
			SXml::WNode w_r(rB.P_Xw, "restriction");
			if(!rInfo.Period.IsZero()) {
				w_r.PutInner("period", EncText((temp_buf = 0).Cat(rInfo.Period, 0)));
			}
			if(rInfo.HasWeekDayRestriction()) {
				temp_buf = 0;
				for(uint d = 0; d < 7; d++) {
					temp_buf.CatChar((rInfo.DaysOfWeek & (1 << d)) ? '1' : '0');
				}
				w_r.PutInner("weekday", EncText(temp_buf));
			}
			if(rInfo.GetTimeRange(tr) > 0) {
				SXml::WNode w_t(rB.P_Xw, "timerange");
				w_t.PutInner("low", (temp_buf = 0).Cat(tr.low, TIMF_HMS));
				w_t.PutInner("upp", (temp_buf = 0).Cat(tr.upp, TIMF_HMS));
			}
			if(!rInfo.AmtRestr.IsZero()) {
				SXml::WNode w_a(rB.P_Xw, "amountrange");
				if(rInfo.AmtRestr.low)
					w_a.PutInner("low", (temp_buf = 0).Cat(rInfo.AmtRestr.low));
				if(rInfo.AmtRestr.upp)
					w_a.PutInner("upp", (temp_buf = 0).Cat(rInfo.AmtRestr.upp));
			}
		}
    }
    return ok;
}

int SLAPI PPPosProtocol::WriteQuotInfo(WriteBlock & rB, const char * pScopeXmlTag, PPID parentObj, const PPQuot & rInfo)
{
    int    ok = -1;
	SString temp_buf;
	if(rInfo.Kind && rInfo.GoodsID) {
		PPQuotKind qk_rec;
		Goods2Tbl::Rec goods_rec;
		if(QkObj.Fetch(rInfo.Kind, &qk_rec) > 0 && GObj.Fetch(rInfo.GoodsID, &goods_rec) > 0) {
			if(goods_rec.Kind == PPGDSK_GOODS || (goods_rec.Kind == PPGDSK_GROUP && !(goods_rec.Flags & GF_ALTGROUP))) {
				SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
				if(parentObj != PPOBJ_QUOTKIND) {
					SXml::WNode w_k(rB.P_Xw, "kind");
					w_k.PutInner("id", (temp_buf = 0).Cat(rInfo.Kind));
				}
				if(goods_rec.Kind == PPGDSK_GOODS && parentObj != PPOBJ_GOODS) {
					SXml::WNode w_g(rB.P_Xw, "ware");
					w_g.PutInner("id", (temp_buf = 0).Cat(rInfo.GoodsID));
				}
				else if(goods_rec.Kind == PPGDSK_GROUP && parentObj != PPOBJ_GOODSGROUP) {
					SXml::WNode w_g(rB.P_Xw, "goodsgroup");
					w_g.PutInner("id", (temp_buf = 0).Cat(rInfo.GoodsID));
				}
				if(rInfo.MinQtty > 0) {
					w_s.PutInner("minqtty", (temp_buf = 0).Cat(rInfo.MinQtty));
				}
				if(!rInfo.Period.IsZero()) {
					w_s.PutInner("period", EncText((temp_buf = 0).Cat(rInfo.Period, 0)));
				}
				w_s.PutInner("value", EncText(rInfo.PutValToStr(temp_buf)));
				ok = 1;
			}
		}
	}
    return ok;
}

int SLAPI PPPosProtocol::WritePersonInfo(WriteBlock & rB, const char * pScopeXmlTag, PPID codeRegTypeID, const PPPersonPacket & rPack)
{
    int    ok = 1;
	SString temp_buf;
	{
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		w_s.PutInner("id", (temp_buf = 0).Cat(rPack.Rec.ID));
		w_s.PutInner("name", EncText(rPack.Rec.Name));
		if(codeRegTypeID) {
            rPack.GetRegNumber(codeRegTypeID, temp_buf);
            if(temp_buf.NotEmptyS()) {
				w_s.PutInner("code", EncText(temp_buf));
            }
		}
	}
    return ok;
}

int SLAPI PPPosProtocol::WriteSCardInfo(WriteBlock & rB, const char * pScopeXmlTag, const AsyncCashSCardInfo & rInfo)
{
	int    ok = 1;
	SString temp_buf;
	{
		SXml::WNode w_s(rB.P_Xw, pScopeXmlTag);
		const int sc_type = 0; // @todo
		w_s.PutInner("id", (temp_buf = 0).Cat(rInfo.Rec.ID));
		w_s.PutInner("code", EncText(rInfo.Rec.Code));
		if(rInfo.Rec.PersonID) {
			PPPersonPacket psn_pack;
			if(PsnObj.GetPacket(rInfo.Rec.PersonID, &psn_pack, 0) > 0) {
				PPID   reg_typ_id = 0;
				PPSCardConfig sc_cfg;
				PPObjSCard::FetchConfig(&sc_cfg);
				if(sc_cfg.PersonKindID) {
					PPObjPersonKind pk_obj;
					PPPersonKind pk_rec;
					if(pk_obj.Fetch(sc_cfg.PersonKindID, &pk_rec) > 0)
						reg_typ_id = pk_rec.CodeRegTypeID;
				}
				THROW(WritePersonInfo(rB, "owner", reg_typ_id, psn_pack));
			}
		}
		if(checkdate(rInfo.Rec.Expiry, 0))
			w_s.PutInner("expiry", (temp_buf = 0).Cat(rInfo.Rec.Expiry, DATF_ISO8601|DATF_CENTURY));
		if(rInfo.Rec.PDis > 0)
			w_s.PutInner("discount", (temp_buf = 0).Cat(fdiv100i(rInfo.Rec.PDis), MKSFMTD(0, 2, NMBF_NOTRAILZ)));
		//if(rInfo.P_QuotByQttyList)
	}
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::StartWriting(const char * pFileName, PPPosProtocol::WriteBlock & rB)
{
	int    ok = 1;
	THROW_LXML(rB.P_Xw = xmlNewTextWriterFilename(pFileName, 0), 0);
	xmlTextWriterSetIndent(rB.P_Xw, 1);
	xmlTextWriterSetIndentString(rB.P_Xw, (const xmlChar*)"\t");
	THROW_MEM(rB.P_Xd = new SXml::WDoc(rB.P_Xw, cpUTF8));
	rB.P_Root = new SXml::WNode(rB.P_Xw, "PapyrusAsyncPosInterchange");
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::FinishWriting(PPPosProtocol::WriteBlock & rB)
{
	rB.Destroy();
	return 1;
}

//static
void PPPosProtocol::Scb_StartDocument(void * ptr)
{
	CALLTYPEPTRMEMB(PPPosProtocol, ptr, StartDocument());
}

//static
void PPPosProtocol::Scb_EndDocument(void * ptr)
{
	CALLTYPEPTRMEMB(PPPosProtocol, ptr, EndDocument());
}

//static
void PPPosProtocol::Scb_StartElement(void * ptr, const xmlChar * pName, const xmlChar ** ppAttrList)
{
	CALLTYPEPTRMEMB(PPPosProtocol, ptr, StartElement((const char *)pName, (const char **)ppAttrList));
}

//static
void PPPosProtocol::Scb_EndElement(void * ptr, const xmlChar * pName)
{
	CALLTYPEPTRMEMB(PPPosProtocol, ptr, EndElement((const char *)pName));
}

//static
void PPPosProtocol::Scb_Characters(void * ptr, const uchar * pC, int len)
{
	CALLTYPEPTRMEMB(PPPosProtocol, ptr, Characters((const char *)pC, len));
}

int PPPosProtocol::StartDocument()
{
	RdB.TagValue = 0;
	return 1;
}

int PPPosProtocol::EndDocument()
{
	return 1;
}

int FASTCALL PPPosProtocol::Helper_PushQuery(int queryType)
{
	int    ok = 1;
	uint   ref_pos = 0;
	assert(oneof2(queryType, QueryBlock::qCSession, QueryBlock::qRefs));
	THROW(RdB.CreateItem(obQuery, &ref_pos));
	RdB.RefPosStack.push(ref_pos);
	{
		int    test_type = 0;
		QueryBlock * p_item = (QueryBlock *)RdB.GetItem(ref_pos, &test_type);
		assert(test_type == obQuery);
		p_item->Q = queryType;
	}
	CATCHZOK
	return ok;
}

PPPosProtocol::QueryBlock * PPPosProtocol::Helper_RenewQuery(uint & rRefPos, int queryType)
{
	QueryBlock * p_blk = 0;
	int    type = 0;
	RdB.RefPosStack.pop(rRefPos);
	THROW(Helper_PushQuery(queryType));
	rRefPos = RdB.RefPosStack.peek();
	p_blk = (QueryBlock *)RdB.GetItem(rRefPos, &type);
	assert(type == obQuery && p_blk && p_blk->Q == queryType);
	CATCH
		p_blk = 0;
	ENDCATCH
	return p_blk;
}

int PPPosProtocol::StartElement(const char * pName, const char ** ppAttrList)
{
	int    ok = 1;
	uint   ref_pos = 0;
    (RdB.TempBuf = pName).ToLower();
    int    tok = 0;
    if(RdB.P_ShT) {
		uint _ut = 0;
		RdB.P_ShT->Search(RdB.TempBuf, &_ut, 0);
		tok = _ut;
    }
    if(tok == PPHS_PPPP_START) {
		RdB.State |= RdB.stHeaderOccured;
	}
	else {
		THROW_PP(RdB.State & RdB.stHeaderOccured, PPERR_FILEISNTPPAPI);
		switch(tok) {
			case PPHS_SOURCE:
				THROW(RdB.CreateItem(obSource, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_DESTINATION:
				THROW(RdB.CreateItem(obDestination, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_QUERYCSESS:
				THROW(Helper_PushQuery(QueryBlock::qCSession));
				break;
			case PPHS_QUERYREFS:
				THROW(Helper_PushQuery(QueryBlock::qRefs));
				break;
			case PPHS_POS:
				THROW(RdB.CreateItem(obPosNode, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_WARE:
				{
					uint   link_ref_pos = RdB.RefPosStack.peek();
					int    link_type = 0;
					void * p_link_item = RdB.GetItem(link_ref_pos, &link_type);

					THROW(RdB.CreateItem(obGoods, &ref_pos));
					RdB.RefPosStack.push(ref_pos);

					if(link_type == obCcLine) {
						int    test_type = 0;
						GoodsBlock * p_item = (GoodsBlock *)RdB.GetItem(ref_pos, &test_type);
						assert(test_type == obGoods);
						p_item->Flags |= ObjectBlock::fRefItem;
					}
				}
				break;
			case PPHS_GOODSGROUP:
				THROW(RdB.CreateItem(obGoodsGroup, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_CARD:
				{
					uint   link_ref_pos = RdB.RefPosStack.peek();
					int    link_type = 0;
					void * p_link_item = RdB.GetItem(link_ref_pos, &link_type);
					THROW(RdB.CreateItem(obSCard, &ref_pos));
					RdB.RefPosStack.push(ref_pos);
					if(link_type == obCCheck) {
						int    test_type = 0;
						SCardBlock * p_item = (SCardBlock *)RdB.GetItem(ref_pos, &test_type);
						assert(test_type == obSCard);
						p_item->Flags |= ObjectBlock::fRefItem;
					}
				}
				break;
			case PPHS_QUOTE:
				{
					uint   link_ref_pos = RdB.RefPosStack.peek();
					int    link_type = 0;
					void * p_link_item = RdB.GetItem(link_ref_pos, &link_type);
					THROW(RdB.CreateItem(obQuot, &ref_pos));
					RdB.RefPosStack.push(ref_pos);
					{
						int    test_type = 0;
						QuotBlock * p_item = (QuotBlock *)RdB.GetItem(ref_pos, &test_type);
						assert(test_type == obQuot);
						if(link_type == obGoods) {
							p_item->GoodsBlkP = link_ref_pos; // ��������� ��������� �� ������� ������, �������� �����������
						}
						else if(link_type == obGoodsGroup) {
							p_item->BlkFlags |= p_item->fGroup;
							p_item->GoodsGroupBlkP = link_ref_pos; // ��������� ��������� �� ������� �������� ������, ������� �����������
						}
					}
				}
				break;
			case PPHS_KIND:
				{
					int  type = 0;
					void * p_item = PeekRefItem(&ref_pos, &type);
					if(type == obQuot) {
						uint   qk_ref_pos = 0;
						THROW(RdB.CreateItem(obQuotKind, &qk_ref_pos));
						{
							int    test_type = 0;
							QuotKindBlock * p_qk_blk = (QuotKindBlock *)RdB.GetItem(qk_ref_pos, &test_type);
							assert(test_type == obQuotKind);
							p_qk_blk->Flags |= ObjectBlock::fRefItem;
						}
						RdB.RefPosStack.push(qk_ref_pos);
					}
				}
				break;
			case PPHS_QUOTEKIND:
				THROW(RdB.CreateItem(obQuotKind, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_CSESSION:
				THROW(RdB.CreateItem(obCSession, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_CC:
				{
					uint   link_ref_pos = RdB.RefPosStack.peek();
					int    link_type = 0;
					void * p_link_item = RdB.GetItem(link_ref_pos, &link_type);
					THROW(RdB.CreateItem(obCCheck, &ref_pos));
					RdB.RefPosStack.push(ref_pos);
					if(link_type == obCSession) {
						int    test_type = 0;
						CCheckBlock * p_item = (CCheckBlock *)RdB.GetItem(ref_pos, &test_type);
						assert(test_type == obCCheck);
						p_item->CSessionBlkP = link_ref_pos; // ��� ��������� �� ������� ������, ������� �����������
					}
				}
				break;
			case PPHS_CCL:
				{
					uint   link_ref_pos = RdB.RefPosStack.peek();
					int    link_type = 0;
					void * p_link_item = RdB.GetItem(link_ref_pos, &link_type);
					THROW(RdB.CreateItem(obCcLine, &ref_pos));
					RdB.RefPosStack.push(ref_pos);
					if(link_type == obCCheck) {
						int    test_type = 0;
						CcLineBlock * p_item = (CcLineBlock *)RdB.GetItem(ref_pos, &test_type);
						assert(test_type == obCcLine);
						p_item->CCheckBlkP = link_ref_pos; // ������ ��������� �� ������� ����, �������� �����������
					}
				}
				break;
			case PPHS_OWNER:
				THROW(RdB.CreateItem(obPerson, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_PARENT:
				THROW(RdB.CreateItem(obParent, &ref_pos));
				RdB.RefPosStack.push(ref_pos);
				break;
			case PPHS_CODE:
				{
					int  type = 0;
					void * p_item = PeekRefItem(&ref_pos, &type);
					if(type == obGoods) {
						uint   goods_code_ref_pos = 0;
						THROW(RdB.CreateItem(obGoodsCode, &goods_code_ref_pos));
						RdB.RefPosStack.push(goods_code_ref_pos);
						{
							int    test_type = 0;
							GoodsCode * p_item = (GoodsCode *)RdB.GetItem(goods_code_ref_pos, &test_type);
							assert(test_type == obGoodsCode);
							p_item->GoodsBlkP = ref_pos; // ��� ��������� �� ������� ������, �������� �����������
						}
					}
				}
				break;
			case PPHS_REFS:
			case PPHS_SYSTEM:
			case PPHS_VERSION:
			case PPHS_UUID:
			case PPHS_TIME:
			case PPHS_ID:
			case PPHS_NAME:
			case PPHS_RANK:
			case PPHS_PRICE:
			case PPHS_DISCOUNT:
			case PPHS_RETURN:
			case PPHS_RESTRICTION:
			case PPHS_PERIOD:
			case PPHS_WEEKDAY:
			case PPHS_TIMERANGE:
			case PPHS_AMOUNTRANGE:
			case PPHS_LOW:
			case PPHS_UPP:
			case PPHS_VALUE:
			case PPHS_EXPIRY:
			case PPHS_FLAGS:
			case PPHS_AMOUNT:
			case PPHS_QTTY:
			case PPHS_SUMDISCOUNT:
			case PPHS_INNERID:
			case PPHS_OBJ:
			case PPHS_LAST:
			case PPHS_CURRENT:
				break;
		}
	}
	RdB.TokPath.push(tok);
	RdB.TagValue = 0;
	CATCH
		SaxStop();
		RdB.State |= RdB.stError;
		ok = 0;
	ENDCATCH
    return ok;
}

void FASTCALL PPPosProtocol::Helper_AddStringToPool(uint * pPos)
{
	if(RdB.TagValue.NotEmptyS()) {
		RdB.AddS(RdB.TagValue, pPos);
	}
}

void * SLAPI PPPosProtocol::PeekRefItem(uint * pRefPos, int * pType) const
{
	const uint ref_pos = RdB.RefPosStack.peek();
	ASSIGN_PTR(pRefPos, ref_pos);
	return RdB.GetItem(ref_pos, pType);
}

int PPPosProtocol::EndElement(const char * pName)
{
	int    tok = 0;
	int    ok = RdB.TokPath.pop(tok);
	uint   ref_pos = 0;
	uint   parent_ref_pos = 0;
	void * p_item = 0;
	int    type = 0;
	int    is_processed = 1;
	switch(tok) {
		case PPHS_PPPP_START:
			RdB.State &= ~RdB.stHeaderOccured;
			break;
		case PPHS_OBJ:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obQuery) {
				QueryBlock * p_blk = (QueryBlock *)p_item;
				if(p_blk->Q == QueryBlock::qRefs) {
					if(RdB.TagValue.NotEmptyS()) {
						long   obj_type_ext = 0;
						PPID   obj_type = DS.GetObjectTypeBySymb(RdB.TagValue, &obj_type_ext);
                        if(obj_type) {
							if(p_blk->ObjType) {
								THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qRefs));
							}
                            p_blk->ObjType = obj_type;
                        }
					}
				}
			}
			break;
		case PPHS_LAST:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obQuery) {
				QueryBlock * p_blk = (QueryBlock *)p_item;
				if(p_blk->Q == QueryBlock::qCSession) {
					if(p_blk->CSess || !p_blk->Period.IsZero()) {
						THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qCSession));
					}
					p_blk->Flags |= QueryBlock::fCSessLast;
				}
			}
			break;
		case PPHS_CURRENT:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obQuery) {
				QueryBlock * p_blk = (QueryBlock *)p_item;
				if(p_blk->Q == QueryBlock::qCSession) {
					if(p_blk->CSess || !p_blk->Period.IsZero()) {
						THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qCSession));
					}
					p_blk->Flags |= QueryBlock::fCSessCurrent;
				}
			}
			break;
		case PPHS_POS:
			RdB.RefPosStack.pop(ref_pos);
			//
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obCSession) {
				((CSessionBlock *)p_item)->PosBlkP = ref_pos;
			}
			break;
		case PPHS_WARE:
			RdB.RefPosStack.pop(ref_pos);
			//
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obCcLine) {
				((CcLineBlock *)p_item)->GoodsBlkP = ref_pos;
			}
			break;
		case PPHS_QUOTE:
			RdB.RefPosStack.pop(ref_pos);
			//
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obGoods) {
				((SCardBlock *)p_item)->OwnerBlkP = ref_pos;
			}
			else if(type == obGoodsGroup) {

			}
			break;
		case PPHS_CARD:
			RdB.RefPosStack.pop(ref_pos);
			//
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obCCheck) {
				((CCheckBlock *)p_item)->SCardBlkP = ref_pos;
			}
			break;
		case PPHS_KIND:
			{
				RdB.RefPosStack.pop(ref_pos);
				int    this_type = 0;
				void * p_this_item = RdB.GetItem(ref_pos, &this_type);
				if(this_type == obQuotKind) {
					//
					// �������� ����� ����������� ��� ���������. ���� �������, �� �������� ���� �������
					// � ���������� ��������� ������
					//
					if(ref_pos == (RdB.RefList.getCount()-1)) {
						const ObjBlockRef obr = RdB.RefList.at(ref_pos); // not ��� obr ������ ��������� ������ - ������� ����� ���� ������ ����
						assert(obr.Type == obQuotKind);
						if(obr.P == (RdB.QkBlkList.getCount()-1)) {
							uint   other_ref_pos = 0;
							if(RdB.SearchAnalogRef_QuotKind(*(QuotKindBlock *)p_this_item, ref_pos, &other_ref_pos)) {
                                RdB.RefList.atFree(ref_pos);
                                RdB.QkBlkList.atFree(obr.P);
								ref_pos = other_ref_pos;
							}
						}
					}
					parent_ref_pos = RdB.RefPosStack.peek();
					p_item = RdB.GetItem(parent_ref_pos, &type);
					if(type == obQuot) {
						((QuotBlock *)p_item)->QuotKindBlkP = ref_pos;
					}
				}
			}
			break;
		case PPHS_OWNER:
			RdB.RefPosStack.pop(ref_pos);
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obSCard) {
				((SCardBlock *)p_item)->OwnerBlkP = ref_pos;
			}
			break;
		case PPHS_PARENT:
			RdB.RefPosStack.pop(ref_pos);
			parent_ref_pos = RdB.RefPosStack.peek();
			p_item = RdB.GetItem(parent_ref_pos, &type);
			if(type == obGoods) {
				((GoodsBlock *)p_item)->ParentBlkP = ref_pos;
			}
			else if(type == obGoodsGroup) {
				((GoodsGroupBlock *)p_item)->ParentBlkP = ref_pos;
			}
			break;
		case PPHS_PERIOD:
			{
				DateRange period;
				if(strtoperiod(RdB.TagValue, &period, 0)) {
					p_item = PeekRefItem(&ref_pos, &type);
					if(type == obQuotKind) {
                        ((QuotKindBlock *)p_item)->Period = period;
					}
					else if(type == obQuery) {
						QueryBlock * p_blk = (QueryBlock *)p_item;
						if(p_blk->Q == QueryBlock::qCSession) {
							if(p_blk->CSess || !p_blk->Period.IsZero() || (p_blk->Flags & (QueryBlock::fCSessCurrent|QueryBlock::fCSessLast))) {
								THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qCSession));
							}
							p_blk->Period = period;
						}
					}
				}
			}
			break;
		case PPHS_LOW:
            {
				int prev_tok = RdB.TokPath.peek();
                if(prev_tok == PPHS_TIMERANGE) {
					LTIME   t = ZEROTIME;
					if(strtotime(RdB.TagValue, TIMF_HMS, &t)) {
						p_item = PeekRefItem(&ref_pos, &type);
						if(type == obQuotKind) {
							((QuotKindBlock *)p_item)->TimeRestriction.low = t;
						}
					}
                }
                else if(prev_tok == PPHS_AMOUNTRANGE) {
					double v = RdB.TagValue.ToReal();
					p_item = PeekRefItem(&ref_pos, &type);
					if(type == obQuotKind) {
						((QuotKindBlock *)p_item)->AmountRestriction.low = v;
					}
                }
            }
			break;
		case PPHS_UPP:
            {
				int prev_tok = RdB.TokPath.peek();
                if(prev_tok == PPHS_TIMERANGE) {
					LTIME   t = ZEROTIME;
					if(strtotime(RdB.TagValue, TIMF_HMS, &t)) {
						p_item = PeekRefItem(&ref_pos, &type);
						if(type == obQuotKind) {
							((QuotKindBlock *)p_item)->TimeRestriction.upp = t;
						}
					}
                }
                else if(prev_tok == PPHS_AMOUNTRANGE) {
					double v = RdB.TagValue.ToReal();
					p_item = PeekRefItem(&ref_pos, &type);
					if(type == obQuotKind) {
						((QuotKindBlock *)p_item)->AmountRestriction.upp = v;
					}
                }
            }
			break;
		case PPHS_ID:
			{
				const long _val_id = RdB.TagValue.ToLong();
				p_item = PeekRefItem(&ref_pos, &type);
				switch(type) {
					case obPosNode: ((PosNodeBlock *)p_item)->ID = _val_id; break;
					case obGoods: ((GoodsBlock *)p_item)->ID = _val_id; break;
					case obGoodsGroup: ((GoodsGroupBlock *)p_item)->ID = _val_id; break;
					case obPerson: ((PersonBlock *)p_item)->ID = _val_id; break;
					case obSCard: ((SCardBlock *)p_item)->ID = _val_id; break;
					case obParent: ((ParentBlock *)p_item)->ID = _val_id; break;
					case obQuotKind: ((QuotKindBlock *)p_item)->ID = _val_id; break;
					case obCSession: ((CSessionBlock *)p_item)->ID = _val_id; break;
					case obCCheck: ((CCheckBlock *)p_item)->ID = _val_id; break;
					case obCcLine: ((CcLineBlock *)p_item)->RByCheck = _val_id; break;
					case obQuery:
						{
							QueryBlock * p_blk = (QueryBlock *)p_item;
							if(p_blk->Q == QueryBlock::qCSession) {
								if(_val_id) {
									if(p_blk->CSess || !p_blk->Period.IsZero() || (p_blk->Flags & (QueryBlock::fCSessCurrent|QueryBlock::fCSessLast))) {
										THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qCSession));
									}
									p_blk->CSess = _val_id;
									p_blk->Flags &= ~QueryBlock::fCSessN;
								}
							}
						}
						break;
				}
			}
			break;
		case PPHS_INNERID:
			{
				const long _val_id = RdB.TagValue.ToLong();
				p_item = PeekRefItem(&ref_pos, &type);
				if(type == obGoods) {
					((GoodsBlock *)p_item)->InnerId = _val_id;
				}
			}
			break;
		case PPHS_VALUE:
			{
				const double _value = RdB.TagValue.ToReal();
				p_item = PeekRefItem(&ref_pos, &type);
				switch(type) {
					case obQuot:
						((QuotBlock *)p_item)->Value = _value;
						break;
				}
			}
			break;
		case PPHS_RANK:
			{
				const long _val_id = RdB.TagValue.ToLong();
				p_item = PeekRefItem(&ref_pos, &type);
				switch(type) {
					case obQuotKind:
						((QuotKindBlock *)p_item)->Rank = (int16)_val_id;
						break;
				}
			}
			break;
		case PPHS_NAME:
			p_item = PeekRefItem(&ref_pos, &type);
			switch(type) {
				case obPosNode: Helper_AddStringToPool(&((PosNodeBlock *)p_item)->NameP); break;
				case obGoods:   Helper_AddStringToPool(&((GoodsBlock *)p_item)->NameP); break;
				case obGoodsGroup: Helper_AddStringToPool(&((GoodsGroupBlock *)p_item)->NameP); break;
				case obPerson: Helper_AddStringToPool(&((PersonBlock *)p_item)->NameP); break;
				case obSCard: break;
				case obQuotKind: Helper_AddStringToPool(&((QuotKindBlock *)p_item)->NameP); break;
			}
			break;
		case PPHS_CODE:
			p_item = PeekRefItem(&ref_pos, &type);
			switch(type) {
				case obPosNode:     Helper_AddStringToPool(&((PosNodeBlock *)p_item)->CodeP); break;
				case obGoodsGroup:  Helper_AddStringToPool(&((GoodsGroupBlock *)p_item)->CodeP); break;
				case obPerson:      Helper_AddStringToPool(&((PersonBlock *)p_item)->CodeP); break;
				case obSCard:       Helper_AddStringToPool(&((SCardBlock *)p_item)->CodeP); break;
				case obParent:      Helper_AddStringToPool(&((ParentBlock *)p_item)->CodeP); break;
				case obQuotKind:    Helper_AddStringToPool(&((QuotKindBlock *)p_item)->CodeP); break;
				case obSource:
				case obDestination: Helper_AddStringToPool(&((RouteObjectBlock *)p_item)->CodeP); break;
				case obGoods:
					break;
				case obGoodsCode:
					Helper_AddStringToPool(&((GoodsCode *)p_item)->CodeP);
					RdB.RefPosStack.pop(ref_pos);
					break;
				case obCSession:
					if(RdB.TagValue.NotEmptyS()) {
						long   icode = RdB.TagValue.ToLong();
						((CSessionBlock *)p_item)->Code = icode;
					}
					break;
				case obCCheck:
					if(RdB.TagValue.NotEmptyS()) {
						long   icode = RdB.TagValue.ToLong();
						((CCheckBlock *)p_item)->Code = icode;
					}
					break;
				case obQuery:
					{
						QueryBlock * p_blk = (QueryBlock *)p_item;
						if(p_blk->Q == QueryBlock::qCSession) {
							const long csess_n = RdB.TagValue.ToLong();
							if(csess_n) {
								if(p_blk->CSess || !p_blk->Period.IsZero() || (p_blk->Flags & (QueryBlock::fCSessCurrent|QueryBlock::fCSessLast))) {
									THROW(p_blk = Helper_RenewQuery(ref_pos, QueryBlock::qCSession));
								}
								p_blk->CSess = csess_n;
								p_blk->Flags |= QueryBlock::fCSessN;
							}
						}
					}
					break;
			}
			break;
		case PPHS_PRICE:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obGoods)
				((GoodsBlock *)p_item)->Price = RdB.TagValue.ToReal();
			else if(type == obCcLine)
				((CcLineBlock *)p_item)->Price = RdB.TagValue.ToReal();
			break;
		case PPHS_DISCOUNT:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obSCard)
				((SCardBlock *)p_item)->Discount = RdB.TagValue.ToReal();
			else if(type == obCCheck)
				((CCheckBlock *)p_item)->Discount = RdB.TagValue.ToReal();
			else if(type == obCcLine)
				((CcLineBlock *)p_item)->Discount = RdB.TagValue.ToReal();
			break;
		case PPHS_RETURN:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obCCheck) {
				((CCheckBlock *)p_item)->SaCcFlags |= CCHKF_RETURN;
			}
			else {
				; // @error
			}
			break;
		case PPHS_SUMDISCOUNT:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obCcLine)
				((CcLineBlock *)p_item)->SumDiscount = RdB.TagValue.ToReal();
			break;
		case PPHS_QTTY:
			p_item = PeekRefItem(&ref_pos, &type);
			if(type == obCcLine)
				((CcLineBlock *)p_item)->Qtty = RdB.TagValue.ToReal();
			break;
		case PPHS_SYSTEM:
			p_item = PeekRefItem(&ref_pos, &type);
			if(oneof2(type, obSource, obDestination)) {
				Helper_AddStringToPool(&((RouteObjectBlock *)p_item)->SystemP);
			}
			break;
		case PPHS_VERSION:
			p_item = PeekRefItem(&ref_pos, &type);
			if(oneof2(type, obSource, obDestination)) {
				Helper_AddStringToPool(&((RouteObjectBlock *)p_item)->VersionP);
			}
			break;
		case PPHS_UUID:
			p_item = PeekRefItem(&ref_pos, &type);
			if(oneof2(type, obSource, obDestination)) {
				if(RdB.TagValue.NotEmptyS()) {
                    S_GUID uuid;
					if(uuid.FromStr(RdB.TagValue)) {
						((RouteObjectBlock *)p_item)->Uuid = uuid;
					}
					else
						; // @error
				}
			}
			break;
		case PPHS_TIME:
			{
				LDATETIME dtm;
                strtodatetime(RdB.TagValue, &dtm, DATF_ISO8601, 0);
				p_item = PeekRefItem(&ref_pos, &type);
				switch(type) {
					case obCSession: ((CSessionBlock *)p_item)->Dtm = dtm; break;
					case obCCheck:   ((CCheckBlock *)p_item)->Dtm = dtm; break;
				}
			}
			break;
		case PPHS_FLAGS:
			p_item = PeekRefItem(&ref_pos, &type);
			switch(type) {
				case obCCheck:
					{
						uint32 _f = 0;
						size_t real_len = 0;
						RdB.TagValue.DecodeHex(0, &_f, sizeof(_f), &real_len);
						if(real_len == sizeof(_f)) {
							((CCheckBlock *)p_item)->CcFlags = _f;
						}
					}
					break;
			}
			break;
		case PPHS_AMOUNT:
			p_item = PeekRefItem(&ref_pos, &type);
			switch(type) {
				case obCCheck:
					{
						const double _value = RdB.TagValue.ToReal();
						((CCheckBlock *)p_item)->Amount = _value;
					}
					break;
				case obCcLine:
					{
						const double _value = RdB.TagValue.ToReal();
						((CcLineBlock *)p_item)->Amount = _value;
					}
					break;
			}
			break;
		case PPHS_GOODSGROUP:
		case PPHS_QUOTEKIND:
		case PPHS_CSESSION:
		case PPHS_CC:
		case PPHS_CCL:
			RdB.RefPosStack.pop(ref_pos);
			break;
		case PPHS_RESTRICTION:
		case PPHS_REFS:
		case PPHS_QUERYCSESS:
		case PPHS_QUERYREFS:
		case PPHS_TIMERANGE:
		case PPHS_AMOUNTRANGE:
			break;
		default:
			is_processed = 0;
			break;
	}
	if(is_processed) {
		(RdB.TempBuf = pName).ToLower();
		if(RdB.P_ShT) {
			uint _ut = 0;
			RdB.P_ShT->Search(RdB.TempBuf, &_ut, 0);
			assert((int)_ut == tok);
		}
	}
	assert(ok);
	CATCH
		SaxStop();
		RdB.State |= RdB.stError;
		ok = 0;
	ENDCATCH
    return ok;
}

int PPPosProtocol::Characters(const char * pS, size_t len)
{
	//
	// ���� ������ ����� ���� �������� ����������� ��������. �� ����� StartElement ��������
	// ����� RdB.TagValue, � ����� ������ ����� ��������� ������������ ������ ��������� ���������
	//
	RdB.TagValue.CatN(pS, len);
	return 1;
}

extern "C" xmlParserCtxtPtr xmlCreateURLParserCtxt(const char * filename, int options);
void xmlDetectSAX2(xmlParserCtxtPtr ctxt); // @prototype

SLAPI PPPosProtocol::ReadBlock::ReadBlock()
{
	P_SaxCtx = 0;
	State = 0;
	P_ShT = PPGetStringHash(PPSTR_HASHTOKEN);
}

SLAPI PPPosProtocol::ReadBlock::~ReadBlock()
{
	Destroy();
	P_ShT = 0;
}

void SLAPI PPPosProtocol::ReadBlock::Destroy()
{
	if(P_SaxCtx) {
		P_SaxCtx->sax = 0;
		xmlFreeDoc(P_SaxCtx->myDoc);
		P_SaxCtx->myDoc = 0;
		xmlFreeParserCtxt(P_SaxCtx);
		P_SaxCtx = 0;
	}
	State = 0;

	TempBuf = 0;
	TagValue = 0;
	TokPath.freeAll();
	RefPosStack.clear();

	SrcBlkList.freeAll();
	DestBlkList.freeAll();
	GoodsBlkList.freeAll();
	GoodsGroupBlkList.freeAll();
	GoodsCodeList.freeAll();
	QkBlkList.freeAll();
	QuotBlkList.freeAll();
	PersonBlkList.freeAll();
	SCardBlkList.freeAll();
	ParentBlkList.freeAll();
	PosBlkList.freeAll();
	CSessBlkList.freeAll();
	CcBlkList.freeAll();
	CclBlkList.freeAll();
	QueryList.freeAll();
	RefList.freeAll();
}

int SLAPI PPPosProtocol::CreateGoodsGroup(const GoodsGroupBlock & rBlk, int isFolder, PPID * pID)
{
	int    ok = -1;
	SString name_buf, code_buf;
	PPID   native_id = 0;
	if(rBlk.NativeID) {
		native_id = rBlk.NativeID;
		ok = 2;
	}
	else {
		Goods2Tbl::Rec gg_rec;
		BarcodeTbl::Rec bc_rec;
		PPID   inner_parent_id = 0;
		if(rBlk.ParentBlkP) {
			int   inner_type = 0;
			ParentBlock * p_inner_blk = (ParentBlock *)RdB.GetItem(rBlk.ParentBlkP, &inner_type);
			assert(p_inner_blk);
			if(p_inner_blk) {
				assert(inner_type == obParent);
				THROW(CreateParentGoodsGroup(*p_inner_blk, 1, &inner_parent_id)); // @recursion
			}
		}
		RdB.GetS(rBlk.NameP, name_buf);
		name_buf.Transf(CTRANSF_UTF8_TO_INNER);
		RdB.GetS(rBlk.CodeP, code_buf);
		code_buf.Transf(CTRANSF_UTF8_TO_INNER);
		if(code_buf.NotEmptyS() && GgObj.SearchCode(code_buf, &bc_rec) > 0 && GgObj.Search(bc_rec.GoodsID, &gg_rec) > 0) {
			if(gg_rec.Kind == PPGDSK_GROUP && !(gg_rec.Flags & GF_ALTGROUP)) {
				//if((isFolder && gg_rec.Flags & GF_FOLDER) || (!isFolder && !(gg_rec.Flags & GF_FOLDER))) {
				{
					native_id = gg_rec.ID;
					ok = 3;
				}
			}
		}
		if(!native_id) {
			PPID   inner_id = 0;
			if(name_buf.NotEmptyS() && GgObj.SearchByName(name_buf, &inner_id, &gg_rec) > 0) {
				if(gg_rec.Kind == PPGDSK_GROUP && !(gg_rec.Flags & GF_ALTGROUP)) {
					//if((isFolder && gg_rec.Flags & GF_FOLDER) || (!isFolder && !(gg_rec.Flags & GF_FOLDER))) {
					{
						native_id = gg_rec.ID;
						ok = 3;
					}
				}
			}
		}
		if(!native_id) {
			PPGoodsPacket gg_pack;
			THROW(GgObj.InitPacket(&gg_pack, isFolder ? gpkndFolderGroup : gpkndOrdinaryGroup, inner_parent_id, 0, code_buf));
			STRNSCPY(gg_pack.Rec.Name, name_buf);
			if(gg_pack.Rec.ParentID) {
				Goods2Tbl::Rec parent_rec;
				if(GgObj.Search(gg_pack.Rec.ParentID, &parent_rec) > 0) {
					if(parent_rec.Kind != PPGDSK_GROUP || !(parent_rec.Flags & GF_FOLDER) || (parent_rec.Flags & (GF_ALTGROUP|GF_EXCLALTFOLD))) {
						gg_pack.Rec.ParentID = 0;
						// @todo message
					}
				}
				else {
					gg_pack.Rec.ParentID = 0;
					// @todo message
				}
			}
			THROW(GgObj.PutPacket(&native_id, &gg_pack, 1));
			ok = 1;
		}
	}
	ASSIGN_PTR(pID, native_id);
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::CreateParentGoodsGroup(const ParentBlock & rBlk, int isFolder, PPID * pID)
{
	int    ok = -1;
	PPID   native_id = 0;
	SString code_buf, temp_buf;
	SString name_buf;
	RdB.GetS(rBlk.CodeP, code_buf);
	code_buf.Transf(CTRANSF_UTF8_TO_INNER);
	if(rBlk.ID || code_buf.NotEmptyS()) {
		for(uint i = 0; i < RdB.GoodsGroupBlkList.getCount(); i++) {
			GoodsGroupBlock & r_grp_blk = RdB.GoodsGroupBlkList.at(i);
			int   this_block = 0;
			if(rBlk.ID) {
				if(r_grp_blk.ID == rBlk.ID) {
					this_block = 1;
				}
			}
			else if(r_grp_blk.CodeP) {
				RdB.GetS(r_grp_blk.CodeP, temp_buf);
				if(code_buf == temp_buf) {
					this_block = 1;
				}
			}
			if(this_block) {
				THROW(ok = CreateGoodsGroup(r_grp_blk, isFolder, &native_id));
				break;
			}
		}
	}
	ASSIGN_PTR(pID, native_id);
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::ResolveGoodsBlock(const GoodsBlock & rBlk, uint refPos, int asRefOnly, PPID defParentID, PPID defUnitID, PPID srcArID, PPID * pNativeID)
{
	int    ok = 1;
	PPID   native_id = rBlk.NativeID;
	SString temp_buf;
	Goods2Tbl::Rec ex_goods_rec;
	Goods2Tbl::Rec parent_rec;
	BarcodeTbl::Rec ex_bc_rec;
	PPIDArray pretend_obj_list;
	if(rBlk.Flags & ObjectBlock::fRefItem) {
		//
		// ��� ��������, ���������� ��� ������ �� ������ ����� ������� � ����� ��, �� ��������� �� �����
		//
		PPID   pretend_id = 0;
		if(rBlk.ID > 0 && GObj.Search(rBlk.ID, &ex_goods_rec) > 0) {
			pretend_id = ex_goods_rec.ID;
		}
		for(uint j = 0; j < RdB.GoodsCodeList.getCount(); j++) {
			const GoodsCode & r_c = RdB.GoodsCodeList.at(j);
			if(r_c.GoodsBlkP == refPos) {
				RdB.GetS(r_c.CodeP, temp_buf);
				if(temp_buf.NotEmptyS()) {
					temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
					if(temp_buf.Len() < sizeof(ex_bc_rec.Code)) {
						if(GObj.SearchByBarcode(temp_buf, &ex_bc_rec, &ex_goods_rec, 0 /* no adopt */) > 0)
							pretend_obj_list.add(ex_goods_rec.ID);
					}
					else {
						; // @error
					}
				}
			}
		}
		if(pretend_obj_list.getCount()) {
			pretend_obj_list.sortAndUndup();
			if(pretend_id) {
				if(pretend_obj_list.lsearch(pretend_id)) {
					native_id = pretend_id; // ��� - ��� �������� ��� ������������� (������������� � �� id � �� ����)
				}
				else {
					// @todo ���� ��������. ������������� ������, �� �� ������������� �����, ���������� ��� ������ � ���
					if(pretend_obj_list.getCount() == 1) {
						native_id = pretend_obj_list.get(0); // ���������� ��������, ��������� �� ����. �� ������, ��
							// ���, ��-�����, ��������, ��� �� ��������������.
					}
					else {
						native_id = pretend_id; // ���� ������������ �� ����� ���������, �� ������������� �������� ��������
					}
				}
			}
			else if(pretend_obj_list.getCount() == 1) {
				native_id = pretend_obj_list.get(0); // ������������ ��� ��� �������������� - ������ �������� ��������
			}
			else {
				// @todo ���� ��������: �������� ��������� ����� � ������� �������������� ���������������
				native_id = pretend_obj_list.getLast(); // ���� ���������� ��������� - �� ������ ����� ����� �����
			}
		}
		else if(pretend_id)
			native_id = pretend_id;
		else {
			; // @err ���������� ����������� ���������� ������ �� �����
		}
	}
	else if(!asRefOnly) {
		PPGoodsPacket goods_pack;
		PPQuotArray quot_list;
		GObj.InitPacket(&goods_pack, gpkndGoods, 0, 0, 0);
		quot_list.GoodsID = 0;
		int    use_ar_code = 0;
		PPID   goods_by_ar_id = 0;
		if(rBlk.ID > 0) {
			(temp_buf = 0).Cat(rBlk.ID);
			if(GObj.P_Tbl->SearchByArCode(srcArID, temp_buf, 0, &ex_goods_rec) > 0) {
				goods_by_ar_id = ex_goods_rec.ID;
			}
			else {
				ArGoodsCodeTbl::Rec new_ar_code;
				MEMSZERO(new_ar_code);
				new_ar_code.ArID = srcArID;
				new_ar_code.Pack = 1;
				STRNSCPY(new_ar_code.Code, temp_buf);
				THROW_SL(goods_pack.ArCodes.insert(&new_ar_code));
			}
			use_ar_code = 1;
		}
		RdB.GetS(rBlk.NameP, temp_buf);
		temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
		STRNSCPY(goods_pack.Rec.Name, temp_buf);
		STRNSCPY(goods_pack.Rec.Abbr, temp_buf);
		if(GObj.SearchByName(temp_buf, 0, &ex_goods_rec) > 0) {
			if(use_ar_code && (/*goods_by_ar_id && */ex_goods_rec.ID != goods_by_ar_id)) {
				THROW(GObj.ForceUndupName(goods_by_ar_id, temp_buf));
				THROW(GObj.UpdateName(ex_goods_rec.ID, temp_buf, 1));
			}
			else if(!use_ar_code) { // ��� ������ � ������� ��������������� ������ �� ������������ �� ��������� � ������
				pretend_obj_list.add(ex_goods_rec.ID);
			}
		}
		{
			PPID   parent_id = 0;
			if(rBlk.ParentBlkP) {
				int   inner_type = 0;
				ParentBlock * p_inner_blk = (ParentBlock *)RdB.GetItem(rBlk.ParentBlkP, &inner_type);
				assert(p_inner_blk);
				if(p_inner_blk) {
					assert(inner_type == obParent);
					THROW(CreateParentGoodsGroup(*p_inner_blk, 0, &parent_id));
				}
			}
			{
				if(GgObj.Fetch(parent_id, &parent_rec) > 0 && parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_FOLDER|GF_EXCLALTFOLD)))
					goods_pack.Rec.ParentID = parent_id;
				else
					goods_pack.Rec.ParentID = defParentID;
			}
		}
		for(uint j = 0; j < RdB.GoodsCodeList.getCount(); j++) {
			const GoodsCode & r_c = RdB.GoodsCodeList.at(j);
			if(r_c.GoodsBlkP == refPos) {
				RdB.GetS(r_c.CodeP, temp_buf);
				if(temp_buf.NotEmptyS()) {
					temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
					if(temp_buf.Len() < sizeof(ex_bc_rec.Code)) {
						THROW(goods_pack.Codes.Add(temp_buf, 0, 1.0));
						if(GObj.SearchByBarcode(temp_buf, &ex_bc_rec, &ex_goods_rec, 0 /* no adopt */) > 0) {
							if(use_ar_code && (goods_by_ar_id && ex_goods_rec.ID != goods_by_ar_id)) {
								THROW(GObj.P_Tbl->RemoveDupBarcode(goods_by_ar_id, temp_buf, 1));
							}
							else
								pretend_obj_list.add(ex_goods_rec.ID);
						}
					}
					else {
						; // @error
					}
				}
			}
		}
		pretend_obj_list.sortAndUndup();
		if(use_ar_code) {
			if(goods_by_ar_id) {
				PPGoodsPacket ex_goods_pack;
				PPID   ex_goods_id = goods_by_ar_id;
				if(GObj.GetPacket(ex_goods_id, &ex_goods_pack, 0) > 0) {
					STRNSCPY(ex_goods_pack.Rec.Name, goods_pack.Rec.Name);
					STRNSCPY(ex_goods_pack.Rec.Abbr, goods_pack.Rec.Abbr);
					if(goods_pack.Rec.ParentID)
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					ex_goods_pack.Codes = goods_pack.Codes;
					//
					// ����������� ������ ����� �� �������:
					// ���� (use_ar_code && goods_by_ar_id), �� ������ ��� ��� ��������� � ex_goods_pack
					// ����� ���� ��� ����� ���� � ���� ���� �� �������, �� �������� � �����������
					// ������� �������������� ������.
					// ����� �������, ������� ����������� �� ������� ����� �� ������� ����� �� ����������.
					//
					if(goods_pack.Rec.ParentID && GgObj.Search(goods_pack.Rec.ParentID, &parent_rec) > 0 &&
						parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_ALTGROUP|GF_FOLDER))) {
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					}
					THROW(GObj.PutPacket(&ex_goods_id, &ex_goods_pack, 1));
					native_id = ex_goods_id;
				}
				else {
					; // @error
				}
			}
			else if(pretend_obj_list.getCount() == 0) {
				PPID   new_goods_id = 0;
				SETIFZ(goods_pack.Rec.UnitID, defUnitID);
				THROW(GObj.PutPacket(&new_goods_id, &goods_pack, 1));
				native_id = new_goods_id;
			}
			else {
				PPGoodsPacket ex_goods_pack;
				PPID   ex_goods_id = pretend_obj_list.get(0);
				if(GObj.GetPacket(ex_goods_id, &ex_goods_pack, 0) > 0) {
					STRNSCPY(ex_goods_pack.Rec.Name, goods_pack.Rec.Name);
					STRNSCPY(ex_goods_pack.Rec.Abbr, goods_pack.Rec.Abbr);
					if(goods_pack.Rec.ParentID)
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					ex_goods_pack.Codes = goods_pack.Codes;
					for(uint bci = 0; bci < goods_pack.Codes.getCount(); bci++) {
						const BarcodeTbl::Rec & r_bc_rec = goods_pack.Codes.at(bci);
						uint   bcp = 0;
						if(ex_goods_pack.Codes.SearchCode(r_bc_rec.Code, &bcp)) {
							ex_goods_pack.Codes.atFree(bcp);
						}
						ex_goods_pack.Codes.insert(&r_bc_rec);
						THROW(GObj.P_Tbl->RemoveDupBarcode(ex_goods_id, r_bc_rec.Code, 1));
					}
					for(uint aci = 0; aci < goods_pack.ArCodes.getCount(); aci++) {
						THROW_SL(ex_goods_pack.ArCodes.insert(&goods_pack.ArCodes.at(aci)));
					}
					if(goods_pack.Rec.ParentID && GgObj.Search(goods_pack.Rec.ParentID, &parent_rec) > 0 &&
						parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_ALTGROUP|GF_FOLDER))) {
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					}
					THROW(GObj.PutPacket(&ex_goods_id, &ex_goods_pack, 1));
					native_id = ex_goods_id;
				}
				else {
					; // @error
				}
			}
		}
		else {
			if(pretend_obj_list.getCount() == 0) {
				PPID   new_goods_id = 0;
				SETIFZ(goods_pack.Rec.UnitID, defUnitID);
				THROW(GObj.PutPacket(&new_goods_id, &goods_pack, 1));
				native_id = new_goods_id;
			}
			else if(pretend_obj_list.getCount() == 1) {
				PPGoodsPacket ex_goods_pack;
				PPID   ex_goods_id = pretend_obj_list.get(0);
				if(GObj.GetPacket(ex_goods_id, &ex_goods_pack, 0) > 0) {
					STRNSCPY(ex_goods_pack.Rec.Name, goods_pack.Rec.Name);
					STRNSCPY(ex_goods_pack.Rec.Abbr, goods_pack.Rec.Abbr);
					if(goods_pack.Rec.ParentID)
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					ex_goods_pack.Codes = goods_pack.Codes;
					if(goods_pack.Rec.ParentID && GgObj.Search(goods_pack.Rec.ParentID, &parent_rec) > 0 &&
						parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_ALTGROUP|GF_FOLDER))) {
						ex_goods_pack.Rec.ParentID = goods_pack.Rec.ParentID;
					}
					THROW(GObj.PutPacket(&ex_goods_id, &ex_goods_pack, 1));
					native_id = ex_goods_id;
				}
				else {
					; // @error
				}
			}
			else {
				; // @error ������������� ����� ����� ���� ����������� ����� ��� ������ ������ � ��.
			}
		}
		if(native_id) {
			quot_list.GoodsID = native_id;
			for(uint k = 0; k < RdB.QuotBlkList.getCount(); k++) {
				const QuotBlock & r_qb = RdB.QuotBlkList.at(k);
				if(r_qb.GoodsBlkP == refPos) {
					assert(!(r_qb.BlkFlags & r_qb.fGroup));
					int    type_qk = 0;
					const QuotKindBlock * p_qk_item = (const QuotKindBlock *)RdB.GetItem(r_qb.QuotKindBlkP, &type_qk);
					if(p_qk_item) {
						assert(type_qk == obQuotKind);
						if(p_qk_item->NativeID) {
							QuotIdent qi(0 /*locID*/, p_qk_item->NativeID, 0 /*curID*/, 0);
							quot_list.SetQuot(qi, r_qb.Value, r_qb.Flags, r_qb.MinQtty, r_qb.Period.IsZero() ? 0 : &r_qb.Period);
						}
					}
				}
			}
			//
			// ������� ���� ��������� � ������ quot_list ��������� �� ������, ���� � ���������� ������ ���������
			// ���� ������� � ������� (���� �������� ��������������� � ������ Price).
			//
			if(rBlk.Price > 0.0) {
				QuotIdent qi(0 /*locID*/, PPQUOTK_BASE, 0 /*curID*/, 0);
				quot_list.SetQuot(qi, rBlk.Price, 0 /*flags*/, 0, 0 /* period */);
			}
			THROW(GObj.PutQuotList(native_id, &quot_list, 1));
		}
	}
	CATCHZOK
	ASSIGN_PTR(pNativeID, native_id);
	return ok;
}

int SLAPI PPPosProtocol::AcceptData(TSCollection <QueryProcessBlock> * pQpBlkList)
{
	int    ok = 1;
	uint   qpb_list_idx = 0;
	SString fmt_buf, msg_buf;
	PPID   src_ar_id = 0; // ������ �������������� �����, ��������������� ��������� ������
	SString temp_buf;
	SString code_buf;
	SString name_buf;
	SString wait_msg_buf;
	PPIDArray temp_id_list;
	PPIDArray pretend_obj_list; // ������ �� ��������, ������� ������������� ��������������
	PPObjUnit u_obj;
	PPObjSCardSeries scs_obj;
	PPObjPersonKind pk_obj;
	PPWait(1);
	{
		//
		// ������ ����� �������� ������ �� ��������� � ����������� ������
		//
		if(RdB.SrcBlkList.getCount() == 1) {
			RouteObjectBlock & r_blk = RdB.SrcBlkList.at(0);
			if(!r_blk.Uuid.IsZero()) {
				// @todo ������� ��� ����� ���������� ������� ������ � �������������� �� ������������� ������ src_ar_id
			}
			if(pQpBlkList) {
				RouteBlock src_;
				RdB.GetRouteItem(r_blk, src_);
				for(uint j = 0; !qpb_list_idx && j < pQpBlkList->getCount(); j++) {
					QueryProcessBlock * p_qpb = pQpBlkList->at(j);
					if(p_qpb->R.IsEqual(src_)) {
						qpb_list_idx = j+1;
					}
				}
				if(!qpb_list_idx) {
					uint _pos = 0;
					QueryProcessBlock * p_new_qpb = pQpBlkList->CreateNewItem(&_pos);
					THROW_SL(p_new_qpb);
					p_new_qpb->R = src_;
					qpb_list_idx = _pos+1;
				}
			}
		}
		else {
			if(RdB.SrcBlkList.getCount() > 1) {
				; // @error
			}
			else { // no src info
				; // @log
			}
		}
		//

		//
		if(RdB.DestBlkList.getCount()) {
			for(uint i = 0; i < RdB.DestBlkList.getCount(); i++) {
			}
		}
	}
	{
		//
		// ������ ����� ���������.
		// 2 ����
		//   1-� ���� - ���� ��������� ��� ����� ObjectBlock::fRefItem
		//   2-� ���� - ���� ��������� �� �������� �� ����� ObjectBlock::fRefItem (��� NativeID)
		//
		PPLoadText(PPTXT_IMPQUOTKIND, wait_msg_buf);
		const uint __count = RdB.QkBlkList.getCount();
		for(uint phase = 0; phase < 2; phase++) {
			for(uint i = 0; i < __count; i++) {
				QuotKindBlock & r_blk = RdB.QkBlkList.at(i);
				if(((phase > 0) || !(r_blk.Flags & r_blk.fRefItem)) && !r_blk.NativeID) {
					uint   ref_pos = 0;
					PPID   native_id = 0;
					PPQuotKind qk_rec;
					if(RdB.SearchRef(obQuotKind, i, &ref_pos)) {
						const QuotKindBlock * p_analog = RdB.SearchAnalog_QuotKind(r_blk);
						if(p_analog) {
							r_blk.NativeID = p_analog->NativeID;
						}
						else {
							PPQuotKindPacket pack;
							RdB.GetS(r_blk.CodeP, code_buf);
							code_buf.Transf(CTRANSF_UTF8_TO_INNER);
							RdB.GetS(r_blk.NameP, name_buf);
							name_buf.Transf(CTRANSF_UTF8_TO_INNER);
							if(code_buf.NotEmptyS()) {
								if(QkObj.SearchBySymb(code_buf, &native_id, &qk_rec) > 0) {
									pack.Rec = qk_rec;
								}
								else
									native_id = 0;
							}
							else if(name_buf.NotEmpty()) {
								if(QkObj.SearchByName(name_buf, &native_id, &qk_rec) > 0) {
									pack.Rec = qk_rec;
								}
								else
									native_id = 0;
							}
							if(name_buf.NotEmpty())
								STRNSCPY(pack.Rec.Name, name_buf);
							if(pack.Rec.Name[0] == 0)
								STRNSCPY(pack.Rec.Name, code_buf);
							if(pack.Rec.Name[0] == 0) {
								(temp_buf = 0).CatChar('#').Cat(r_blk.ID);
								STRNSCPY(pack.Rec.Name, temp_buf);
							}
							STRNSCPY(pack.Rec.Symb, code_buf);
							pack.Rec.Rank = r_blk.Rank;
							pack.Rec.SetAmtRange(&r_blk.AmountRestriction);
							pack.Rec.SetTimeRange(r_blk.TimeRestriction);
							pack.Rec.Period = r_blk.Period;
							THROW(QkObj.PutPacket(&native_id, &pack, 1));
							r_blk.NativeID = native_id;
						}
					}
				}
				PPWaitPercent((phase * __count) + i+1, __count * 2, wait_msg_buf);
			}
		}
	}
	{
		//
		// ������ �������� �����
		//
		{
			//
			// ������� �������� ���� �� ������� � ����� ������� ��� ���������������� ��� ������-�����
			//
			PPLoadText(PPTXT_IMPGOODSGRP, wait_msg_buf);
			const uint __count = RdB.GoodsGroupBlkList.getCount();
			for(uint i = 0; i < __count; i++) {
				GoodsGroupBlock & r_blk = RdB.GoodsGroupBlkList.at(i);
				uint   ref_pos = 0;
				if(RdB.SearchRef(obGoodsGroup, i, &ref_pos)) {
					PPID   parent_id = 0;
					if(r_blk.ParentBlkP) {
						int   inner_type = 0;
						ParentBlock * p_inner_blk = (ParentBlock *)RdB.GetItem(r_blk.ParentBlkP, &inner_type);
						assert(p_inner_blk);
						if(p_inner_blk) {
							assert(inner_type == obParent);
							THROW(CreateParentGoodsGroup(*p_inner_blk, 1, &parent_id));
						}
					}
				}
				else {
					; // @error
				}
				PPWaitPercent(i+1, __count * 2, wait_msg_buf);
			}
		}
		{
			//
			// ������ �������� ��� ������ ������� ������ (����� ��� ���������������� �� ���������� �������)
			//
			PPLoadText(PPTXT_IMPGOODSGRP, wait_msg_buf);
			const uint __count = RdB.GoodsGroupBlkList.getCount();
			for(uint i = 0; i < __count; i++) {
				GoodsGroupBlock & r_blk = RdB.GoodsGroupBlkList.at(i);
				if(r_blk.NativeID == 0) {
					uint   ref_pos = 0;
					if(RdB.SearchRef(obGoodsGroup, i, &ref_pos)) {
						THROW(CreateGoodsGroup(r_blk, 0, &r_blk.NativeID));
					}
					else {
						; // @error
					}
				}
				PPWaitPercent(__count+i+1, __count * 2, wait_msg_buf);
			}
		}
	}
	{
		//
		// ������ �������
		//
		PPGoodsPacket goods_pack;
		PPQuotArray quot_list;
		Goods2Tbl::Rec parent_rec;
		PPUnit u_rec;
		SString def_parent_name;
		SString def_unit_name;
		PPID   def_parent_id = GObj.GetConfig().DefGroupID;
		PPID   def_unit_id = GObj.GetConfig().DefUnitID;
		if(def_unit_id && u_obj.Search(def_unit_id, &u_rec) > 0) {
			; // @ok
		}
		else {
			long   def_counter = 0;
			def_unit_id = 0;
			def_unit_name = "default";
			while(!def_parent_id && u_obj.SearchByName(def_unit_name, 0, &u_rec) > 0) {
				if(u_rec.Flags & PPUnit::Trade) {
					def_unit_id = u_rec.ID;
				}
				else
					(def_unit_name = "default").CatChar('-').CatLongZ(++def_counter, 3);
			}
		}
		if(def_parent_id && GgObj.Search(def_parent_id, &parent_rec) > 0 && parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_ALTGROUP|GF_FOLDER))) {
			; // @ok
		}
		else {
			long   def_counter = 0;
			def_parent_id = 0;
			def_parent_name = "default";
			while(!def_parent_id && GgObj.SearchByName(def_parent_name, 0, &parent_rec) > 0) {
				if(parent_rec.Kind == PPGDSK_GROUP && !(parent_rec.Flags & (GF_ALTGROUP|GF_FOLDER))) {
					def_parent_id = parent_rec.ID;
				}
				else
					(def_parent_name = "default").CatChar('-').CatLongZ(++def_counter, 3);
			}
		}
		if(!def_parent_id || def_parent_id != GObj.GetConfig().DefGroupID || !def_unit_id || def_unit_id != GObj.GetConfig().DefUnitID) {
			PPTransaction tra(1);
			THROW(tra);
			if(!def_parent_id) {
				THROW(GgObj.AddSimple(&def_parent_id, gpkndOrdinaryGroup, 0, def_parent_name, 0, 0, 0));
			}
			if(!def_unit_id) {
				THROW(u_obj.AddSimple(&def_unit_id, def_unit_name, PPUnit::Trade, 0));
			}
			if(def_parent_id != GObj.GetConfig().DefGroupID || def_unit_id != GObj.GetConfig().DefUnitID) {
				PPGoodsConfig new_cfg;
				GObj.ReadConfig(&new_cfg);
				new_cfg.DefGroupID = def_parent_id;
				new_cfg.DefUnitID = def_unit_id;
				THROW(GObj.WriteConfig(&new_cfg, 0, 0));
			}
			THROW(tra.Commit());
		}
		assert(def_parent_id > 0);
		assert(def_unit_id > 0);
		{
			PPLoadText(PPTXT_IMPGOODS, wait_msg_buf);
			const uint __count = RdB.GoodsBlkList.getCount();
			for(uint i = 0; i < __count; i++) {
				GoodsBlock & r_blk = RdB.GoodsBlkList.at(i);
				uint   ref_pos = 0;
				if(RdB.SearchRef(obGoods, i, &ref_pos)) {
					PPID   native_id = 0;
					THROW(ResolveGoodsBlock(r_blk, ref_pos, 0, def_parent_id, def_unit_id, src_ar_id, &native_id));
					r_blk.NativeID = native_id;
				}
				else {
					; // @error
				}
				PPWaitPercent(i+1, __count, wait_msg_buf);
			}
		}
	}
	{
		//
		// ������ ������������ ����
		//
		SString def_dcard_ser_name;
		SString def_ccard_ser_name;
		PPSCardConfig sc_cfg;
		SCardTbl::Rec _ex_sc_rec;
		PPSCardSeries scs_rec;
		PPPersonPacket psn_pack;
		ScObj.FetchConfig(&sc_cfg);
		PPID   def_dcard_ser_id = sc_cfg.DefSerID; // ����� ���������� ���� �� ���������
		PPID   def_ccard_ser_id = sc_cfg.DefCreditSerID; // ����� ��������� ���� �� ���������
		if(def_dcard_ser_id && scs_obj.Search(def_dcard_ser_id, &scs_rec) > 0) {
			; // @ok
		}
		else {
			long   def_counter = 0;
			def_dcard_ser_id = 0;
			def_dcard_ser_name = "default";
			while(!def_dcard_ser_id && scs_obj.SearchByName(def_dcard_ser_name, 0, &scs_rec) > 0) {
				def_dcard_ser_id = scs_rec.ID;
			}
		}
		if(def_ccard_ser_id && scs_obj.Search(def_ccard_ser_id, &scs_rec) > 0) {
			; // @ok
		}
		else {
			long   def_counter = 0;
			def_ccard_ser_id = 0;
			def_ccard_ser_name = "default-credit";
			while(!def_ccard_ser_id && scs_obj.SearchByName(def_ccard_ser_name, 0, &scs_rec) > 0) {
				if(scs_rec.GetType() == scstCredit)
					def_ccard_ser_id = scs_rec.ID;
				else
					(def_ccard_ser_name = "default-credit").CatChar('-').CatLongZ(++def_counter, 3);
			}
		}
		if(!def_dcard_ser_id || !def_ccard_ser_id) {
			PPTransaction tra(1);
			THROW(tra);
			{
				ScObj.ReadConfig(&sc_cfg);
				if(!def_dcard_ser_id) {
					PPSCardSerPacket scs_pack;
					STRNSCPY(scs_pack.Rec.Name, def_dcard_ser_name);
					scs_pack.Rec.SetType(scstDiscount);
					scs_pack.Rec.PersonKindID = PPPRK_CLIENT;
					THROW(scs_obj.PutPacket(&def_dcard_ser_id, &scs_pack, 0));
					sc_cfg.DefSerID = def_dcard_ser_id;
				}
				if(!def_ccard_ser_id) {
					PPSCardSerPacket scs_pack;
					STRNSCPY(scs_pack.Rec.Name, def_ccard_ser_name);
					scs_pack.Rec.SetType(scstCredit);
					scs_pack.Rec.PersonKindID = PPPRK_CLIENT;
					THROW(scs_obj.PutPacket(&def_ccard_ser_id, &scs_pack, 0));
					sc_cfg.DefCreditSerID = def_ccard_ser_id;
				}
				THROW(ScObj.WriteConfig(&sc_cfg, 0));
			}
			THROW(tra.Commit());
		}
		{
			PPLoadText(PPTXT_IMPSCARD, wait_msg_buf);
			const uint __count = RdB.SCardBlkList.getCount();
			for(uint i = 0; i < __count; i++) {
				SCardBlock & r_blk = RdB.SCardBlkList.at(i);
				uint   ref_pos = 0;
				if(RdB.SearchRef(obSCard, i, &ref_pos)) {
					if(r_blk.Flags & ObjectBlock::fRefItem) {
						//
						// ��� ��������, ���������� ��� ������ �� ������ ����� ������� � ����� ��, �� ��������� �� �����
						//
						RdB.GetS(r_blk.CodeP, temp_buf);
						temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
						if(ScObj.SearchCode(0, temp_buf, &_ex_sc_rec) > 0) {
							//
							// ����� ��� ������ - ����� �� ����, ������ ���� �����
							// Note: ������, �� �������������� �� ��������� �������� �� ������������ ������� ����.
							//
							r_blk.NativeID = _ex_sc_rec.ID;
						}
					}
					else {
						//SCardTbl::Rec sc_pack;
						//MEMSZERO(sc_pack);
						PPSCardPacket sc_pack;
						pretend_obj_list.clear();
						RdB.GetS(r_blk.CodeP, temp_buf);
						temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
						STRNSCPY(sc_pack.Rec.Code, temp_buf);
						if(r_blk.Discount != 0.0) {
							sc_pack.Rec.PDis = R0i(r_blk.Discount * 100);
						}
						assert(def_dcard_ser_id);
						sc_pack.Rec.SeriesID = def_dcard_ser_id;
						if(r_blk.OwnerBlkP) {
							PPPersonKind pk_rec;
							PPID   owner_status_id = PPPRS_PRIVATE;
							int    ref_type = 0;
							PersonBlock * p_psn_blk = (PersonBlock *)RdB.GetItem(r_blk.OwnerBlkP, &ref_type);
							assert(p_psn_blk);
							assert(ref_type == obPerson);
							if(p_psn_blk->NativeID) {
								sc_pack.Rec.PersonID = p_psn_blk->NativeID;
							}
							else {
								const PersonBlock * p_analog = RdB.SearchAnalog_Person(*p_psn_blk);
								if(p_analog) {
									sc_pack.Rec.PersonID = p_analog->NativeID;
									p_psn_blk->NativeID = p_analog->NativeID;
								}
							}
							if(!sc_pack.Rec.PersonID) {
								psn_pack.destroy();
								psn_pack.Rec.Status = owner_status_id;
								RdB.GetS(p_psn_blk->CodeP, code_buf);
								code_buf.Transf(CTRANSF_UTF8_TO_INNER);
								RdB.GetS(p_psn_blk->NameP, temp_buf);
								temp_buf.Transf(CTRANSF_UTF8_TO_INNER);
								STRNSCPY(psn_pack.Rec.Name, temp_buf);
								PPID   owner_kind_id = (sc_pack.Rec.SeriesID && scs_obj.Fetch(sc_pack.Rec.SeriesID, &scs_rec) > 0) ? scs_rec.PersonKindID : 0;
								SETIFZ(owner_kind_id, ScObj.GetConfig().PersonKindID);
								SETIFZ(owner_kind_id, PPPRK_CLIENT);
								if(owner_kind_id && pk_obj.Search(owner_kind_id, &pk_rec) > 0) {
									PersonTbl::Rec psn_rec;
									PPID   reg_type_id = pk_rec.CodeRegTypeID;
									temp_id_list.clear();
									if(code_buf.NotEmptyS() && reg_type_id && PsnObj.GetListByRegNumber(reg_type_id, owner_kind_id, code_buf, temp_id_list) > 0) {
										if(temp_id_list.getCount() == 1)
											sc_pack.Rec.PersonID = temp_id_list.getSingle();
										else if(temp_id_list.getCount() > 1) {
											for(uint k = 0; !sc_pack.Rec.PersonID && k < temp_id_list.getCount(); k++) {
												if(PsnObj.Search(temp_id_list.get(k), &psn_rec) > 0 && stricmp866(psn_rec.Name, psn_pack.Rec.Name) == 0)
													sc_pack.Rec.PersonID = psn_rec.ID;
											}
										}
									}
									if(!sc_pack.Rec.PersonID && psn_pack.Rec.Name[0]) {
										temp_id_list.clear();
										temp_id_list.add(owner_kind_id);
										if(PsnObj.SearchFirstByName(psn_pack.Rec.Name, &temp_id_list, 0, &psn_rec) > 0)
											sc_pack.Rec.PersonID = psn_rec.ID;
									}
									if(sc_pack.Rec.PersonID) {
										PPPersonPacket ex_psn_pack;
										THROW(PsnObj.GetPacket(sc_pack.Rec.PersonID, &ex_psn_pack, 0) > 0);
										STRNSCPY(ex_psn_pack.Rec.Name, psn_pack.Rec.Name);
										if(reg_type_id && code_buf.NotEmptyS()) {
											THROW(ex_psn_pack.AddRegister(reg_type_id, code_buf, 1));
										}
										psn_pack.Kinds.addUnique(owner_kind_id);
										THROW(PsnObj.PutPacket(&sc_pack.Rec.PersonID, &ex_psn_pack, 1));
									}
									else {
										if(reg_type_id && code_buf.NotEmptyS()) {
											RegisterTbl::Rec reg_rec;
											MEMSZERO(reg_rec);
											reg_rec.RegTypeID = reg_type_id;
											STRNSCPY(reg_rec.Num, code_buf);
										}
										psn_pack.Kinds.addUnique(owner_kind_id);
										THROW(PsnObj.PutPacket(&sc_pack.Rec.PersonID, &psn_pack, 1));
									}
									p_psn_blk->NativeID = sc_pack.Rec.PersonID;
								}
								else {
									; // @error �� ������� ������� ����������-��������� ����� ��-�� �� �������������� ����
								}
							}
						}
						if(ScObj.SearchCode(0, sc_pack.Rec.Code, &_ex_sc_rec) > 0) {
							pretend_obj_list.add(_ex_sc_rec.ID);
						}
						//
						if(pretend_obj_list.getCount() == 0) {
							PPID   new_sc_id = 0;
							if(ScObj.PutPacket(&new_sc_id, &sc_pack, 1)) {
								r_blk.NativeID = new_sc_id;
							}
							else {
								; // @error
							}
						}
						else if(pretend_obj_list.getCount() == 1) {
							PPID   sc_id = pretend_obj_list.get(0);
							PPSCardPacket ex_sc_pack;
							THROW(ScObj.GetPacket(sc_id, &ex_sc_pack) > 0);
							STRNSCPY(ex_sc_pack.Rec.Code, sc_pack.Rec.Code);
							ex_sc_pack.Rec.PDis = sc_pack.Rec.PDis;
							ex_sc_pack.Rec.PersonID = sc_pack.Rec.PersonID;
							if(ScObj.PutPacket(&sc_id, &ex_sc_pack, 1)) {
								r_blk.NativeID = sc_id;
							}
							else {
								; // @error
							}
						}
						else {
							; // @error ������������� ����� ����� ���� ������������ ����� ��� ����� ����� � ��.
						}
					}
				}
				else {
					; // @error
				}
				PPWaitPercent(i+1, __count, wait_msg_buf);
			}
		}
	}
	{
		//
		// ������ �������� ������
		//
		for(uint i = 0; i < RdB.CSessBlkList.getCount(); i++) {
			CSessionBlock & r_blk = RdB.CSessBlkList.at(i);
			uint   ref_pos = 0;
			if(RdB.SearchRef(obCSession, i, &ref_pos)) {
				for(uint ccidx = 0; ccidx < RdB.CcBlkList.getCount(); ccidx++) {
					CCheckBlock & r_cc_blk = RdB.CcBlkList.at(ccidx);
					if(r_cc_blk.CSessionBlkP == ref_pos) {
						uint   cc_ref_pos = 0;
						if(RdB.SearchRef(obCCheck, i, &cc_ref_pos)) {
							for(uint clidx = 0; clidx < RdB.CclBlkList.getCount(); clidx++) {
								CcLineBlock & r_cl_blk = RdB.CclBlkList.at(clidx);
								if(r_cl_blk.CCheckBlkP == cc_ref_pos) {

								}
							}
						}
					}
				}
			}
		}
	}
	CATCHZOK
	RdB.Destroy();
	PPWait(0);
    return ok;
}

int PPPosProtocol::SaxStop()
{
	xmlStopParser(RdB.P_SaxCtx);
	return 1;
}

int SLAPI PPPosProtocol::SaxParseFile(const char * pFileName)
{
    int    ok = 1;
    SString msg_buf;
    PPWait(1);
	xmlSAXHandler saxh;
	MEMSZERO(saxh);
	saxh.startDocument = Scb_StartDocument;
	saxh.endDocument = Scb_EndDocument;
	saxh.startElement = Scb_StartElement;
	saxh.endElement = Scb_EndElement;
	saxh.characters = Scb_Characters;

	PPFormatT(PPTXT_POSPROT_PARSING, &msg_buf, pFileName);
	PPWaitMsg(msg_buf);
	xmlFreeParserCtxt(RdB.P_SaxCtx);
	THROW(RdB.P_SaxCtx = xmlCreateURLParserCtxt(pFileName, 0));
	if(RdB.P_SaxCtx->sax != (xmlSAXHandlerPtr)&xmlDefaultSAXHandler)
		free(RdB.P_SaxCtx->sax);
	RdB.P_SaxCtx->sax = &saxh;
	xmlDetectSAX2(RdB.P_SaxCtx);
	RdB.P_SaxCtx->userData = this;
	xmlParseDocument(RdB.P_SaxCtx);
	THROW_LXML(RdB.P_SaxCtx->wellFormed, RdB.P_SaxCtx);
	THROW(RdB.P_SaxCtx->errNo == 0);
	THROW(!(RdB.State & RdB.stError));
	// THROW(AcceptData());
	CATCHZOK
	//RdB.Destroy();
	PPWait(0);
    return ok;
}

void SLAPI PPPosProtocol::DestroyReadBlock()
{
    RdB.Destroy();
}

int SLAPI PPPosProtocol::ProcessInput(PPPosProtocol::ProcessInputBlock & rPib)
{
	int    ok = -1;
#ifndef NDEBUG // {
	const char * p_base_name = "pppp";
	const char * p_done_suffix = "-done";
	SString in_file_name;
	SString temp_buf;
	SPathStruc ps;
	//PPPosProtocol pp;
	PPWait(1);
	{
		SFileEntryPool fep;
		SString in_path;
		SDirEntry de;
		SString done_plus_xml_suffix;
		(done_plus_xml_suffix = p_done_suffix).Dot().Cat("xml");
		THROW(PPGetPath(PPPATH_IN, in_path));
		(temp_buf = in_path).SetLastSlash().Cat(p_base_name).Cat("*.xml");
        for(SDirec sd(temp_buf, 0); sd.Next(&de) > 0;) {
			if(de.IsFile()) {
                const size_t fnl = strlen(de.FileName);
                if(fnl <= done_plus_xml_suffix.Len() || !sstreqi_ascii(de.FileName+fnl-done_plus_xml_suffix.Len(), done_plus_xml_suffix)) {
					THROW_SL(fep.Add(in_path, de));
                }
			}
        }
		fep.Sort(SFileEntryPool::scByWrTime/*|SFileEntryPool::scDesc*/);
		{
			SFileEntryPool::Entry fe;
			for(uint i = 0; i < fep.GetCount(); i++) {
				if(fep.Get(i, fe)) {
					(in_file_name = fe.Path).SetLastSlash().Cat(fe.Name);
					if(fileExists(in_file_name)) {
						DestroyReadBlock();
						if(SaxParseFile(in_file_name)) {
							if(AcceptData(&rPib.QpBlkList)) {
								if(rPib.Flags & rPib.fRenameProcessed) {
									ps.Split(in_file_name);
									ps.Nam.CatChar('-').Cat("done");
									ps.Merge(temp_buf);
									if(fileExists(temp_buf))
										SFile::Remove(temp_buf);
									SFile::Rename(in_file_name, temp_buf);
								}
							}
							else
								PPLogMessage(PPFILNAM_ERR_LOG, 0, LOGMSGF_LASTERR|LOGMSGF_DBINFO|LOGMSGF_TIME|LOGMSGF_USER);
						}
					}
				}
			}
		}
	}
	PPWait(0);
	CATCH
		ok = PPErrorZ();
	ENDCATCH
#endif // } !NDEBUG
	return ok;
}

int SLAPI PPPosProtocol::SelectOutFileName(const char * pPath, const char * pInfix, SString & rResult)
{
	rResult = 0;
	const char * p_base_name = "pppp";
	int    ok = 1;
	SString temp_buf;
	SString path;
	if(isempty(pPath)) {
		THROW(PPGetPath(PPPATH_OUT, path));
	}
	else
		path = pPath;
	{
		long   _seq = 0;
		do {
			temp_buf = p_base_name;
			if(!isempty(pInfix))
				temp_buf.CatChar('-').Cat(pInfix);
			if(_seq)
				temp_buf.CatChar('-').Cat(_seq);
			temp_buf.Dot().Cat("xml");
			(rResult = path).SetLastSlash().Cat(temp_buf);
			_seq++;
		} while(::fileExists(rResult));
	}
	CATCHZOK
	return ok;
}

int SLAPI PPPosProtocol::ExportPosSession(const PPIDArray & rSessList)
{
	int    ok = -1;
// @v9.6.2 #ifndef NDEBUG // {
	SString out_file_name;
	PPPosProtocol::WriteBlock wb;
	THROW(SelectOutFileName(0, "csess", out_file_name));
	{
		THROW(StartWriting(out_file_name, wb));
		{
			THROW(WriteSourceRoute(wb));
			{
				PPPosProtocol::RouteBlock rb;
				THROW(WriteRouteInfo(wb, "destination", rb));
			}
		}
		{
			PPObjCSession cs_obj;
			CSessionTbl::Rec cs_rec;
			for(uint i = 0; i < rSessList.getCount(); i++) {
				const PPID sess_id = rSessList.get(i);
				if(cs_obj.Search(sess_id, &cs_rec) > 0) {
					if(!WriteCSession(wb, "csession", cs_rec)) {
						; // @todo logerror
					}
				}
			}
		}
		FinishWriting(wb);
	}
	ok = 1;
	CATCHZOK
// @v9.6.2 #endif // } !NDEBUG
	return ok;
}

