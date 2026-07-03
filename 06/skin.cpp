	//sk_cb.mValue, sk_cr.mValue  肤色中心
	//sk_norm_en.mValues是不同距，可以先用=0；  sk_diff_sel.mValue也是和距相关不同方法，可以先用=0
	//sk_th0.mValue 只是一个映射，默认可以th0=100;  th1=180;  th2=200; th3=220; 当然这些可以调试
	
	int diff_cb = abs(cb - sk_cb.mValue);
	int diff_cr = abs(cr - sk_cr.mValue);

	int diff_cb_norm = (cb < sk_cb.mValue) ? diff_cb :  (diff_cb << 1);  
	int diff_cr_norm = (cr > sk_cr.mValue) ? diff_cr :  (diff_cr << 1);

	if (sk_norm_en.mValue == 0)
	{
		diff_cb_norm = diff_cb;
		diff_cr_norm = diff_cr;
	}

	int diff_sum = (diff_cb_norm + diff_cr_norm);
	int diff_max = max(diff_cb_norm, diff_cr_norm);

	int diff_out;
	switch (sk_diff_sel.mValue) 
	{
	case 0:
		{
			diff_out = (cb < sk_cb.mValue && cr > sk_cr.mValue) ? diff_max :
				(cb > sk_cb.mValue && cr < sk_cr.mValue) ? diff_max : diff_sum;
		}
		break;
	case 1:
		{
			diff_out = diff_max;
		}
		break;
	case 2:
		{
			diff_out = diff_sum;
		}
		break;
	}

	//alpha mapping
	diff_out = 
		(y < sk_th0.mValue) ? diff_out*32:
		(y < sk_th1.mValue) ? diff_out*16 : 
		(y < sk_th2.mValue) ? diff_out*8 : 
		(y < sk_th3.mValue) ? diff_out*4 : diff_out*2 ;

	diff_out = diff_out >> 1;
	diff_out = CLIP(diff_out,0,255);

//best try :  sk_norm_en.mValue = 0    sk_diff_sel.mValue=2     diff_out*16
