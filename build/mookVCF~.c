//
//  mookVCF.cpp
//  max-external
//
//  Created by Nicolas Brochec on 25/07/2022.
//
//  This is free and unencumbered software released into the public domain.
//

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

static t_class *mookVCF_class; // création de la classe


typedef struct _mookVCF
{
    t_pxobject s_ob;
    double s_freq;
    double s_res;
    double s_fqterm;
    double s_p, s_k, s_ym1, s_ym2, s_ym3, s_ym4, s_lp, s_lk;
    double s_lx, s_ly1, s_ly2, s_ly3;
    double s_resterm;
    double s_t1, s_t2;
    double connected;
    double s_fcon;
    double s_rcon;
    double s_sr;
    
}t_mookVCF;

void mookVCF_float(t_mookVCF *x, double f);
void mookVCF_int(t_mookVCF *x, long n);
void mookVCF_free(t_mookVCF *x);
void mookVCF_clear(t_mookVCF *x);
void mookVCF_calc(t_mookVCF *x);
void mookVCF_dsp64(t_mookVCF *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void mookVCF_perform64(t_mookVCF *x,t_object *dsp64,double **ins,long numins,double **outs,long numouts,long sampleframes, long flags,void  *userparam);
t_max_err mookVCF_attr_setcutoff(t_mookVCF *x, void *attr, long argc, t_atom *argv);
t_max_err mookVCF_attr_setresonance(t_mookVCF *x, void *attr, long argc, t_atom *argv);
void mookVCF_assist(t_mookVCF *x, void *b, long m, long a, char *s);
void *mookVCF_new(t_symbol *s, long argc, t_atom *argv);

//　エクスターナル構造
C74_EXPORT void ext_main(void *r){
    t_class *c;
    c = class_new("mookVCF~", (method)mookVCF_new, (method)dsp_free, (long)sizeof(t_mookVCF),0L,A_GIMME,0);
    class_addmethod(c,(method)mookVCF_float,"float", A_FLOAT,0);
    class_addmethod(c,(method)mookVCF_int,"int", A_LONG, 0);
    class_addmethod(c,(method)mookVCF_dsp64,"dsp64", A_CANT, 0);
    class_addmethod(c,(method)mookVCF_clear, "clear", 0);
    class_addmethod(c,(method)mookVCF_assist, "assist", A_CANT, 0);

    CLASS_ATTR_DOUBLE(c, "cutoff",0, t_mookVCF, s_freq);
    CLASS_ATTR_BASIC(c, "cutoff", 0);
    CLASS_ATTR_LABEL(c, "cutoff", 0, "Cutoff Frequency");
//    CLASS_ATTR_STYLE_LABEL(c, "cutoff", 0, "Cutoff Frequency", "Cutoff Frequency");
    CLASS_ATTR_ALIAS(c, "cutoff", "freq");
    CLASS_ATTR_ACCESSORS(c, "cutoff", 0, mookVCF_attr_setcutoff);

    CLASS_ATTR_DOUBLE(c, "resonance", 0, t_mookVCF, s_res);
    CLASS_ATTR_BASIC(c, "resonance", 0);
    CLASS_ATTR_LABEL(c, "resonance", 0, "Resonance");
//    CLASS_ATTR_STYLE_LABEL(c, "resonance", 0, "Resonance", "Resonance");
    CLASS_ATTR_ALIAS(c, "resonance", "q");
    CLASS_ATTR_ACCESSORS(c, "resonance", 0, mookVCF_attr_setresonance);
    class_dspinit(c);
    
    class_register(CLASS_BOX,c);
    mookVCF_class=c;
    
}
void mookVCF_free(t_mookVCF *x)
{
    ;
}

void mookVCF_dsp64(t_mookVCF *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags){
   
    x->s_sr = samplerate;
    mookVCF_clear(x);
    x->s_lp = x->s_p;
    x->s_lk = x->s_k;
    x->s_fcon = count[1];
    x->s_rcon = count[2];
    mookVCF_calc(x);
    object_method(dsp64, gensym("dsp_add64"), x, mookVCF_perform64, 0, NULL);
}

void mookVCF_int(t_mookVCF *x, long n){
    mookVCF_float(x, (double)n);
}

void mookVCF_float(t_mookVCF *x, double f){

    long in = proxy_getinlet((t_object *)x);
    
    if(in==1){
        x->s_freq= f < 1. ? 1 : f;
        x->s_freq=f;
        object_attr_touch((t_object *)x, gensym("cutoff"));
        mookVCF_calc(x);
    }else if(in==2){
        if(f >= 1){
            f = 1.f - 1E-20;
            x->s_res = f;
        }else if(f<0.){
            f = 0.f;
            x->s_res = f;
        }else{
            x->s_res = f;
        }
        
        object_attr_touch((t_object *)x, gensym("resonance"));
        mookVCF_calc(x);
        }
}

t_max_err mookVCF_attr_setcutoff(t_mookVCF *x, void *attr, long argc, t_atom *argv){
    double freq = atom_getfloat(argv);
    
    x->s_freq = freq < 1. ? 1 : freq;
    mookVCF_calc(x);
    return 0.;
}

t_max_err mookVCF_attr_setresonance(t_mookVCF *x, void *attr, long argc, t_atom *argv){
    double reso = atom_getfloat(argv);

    if(reso >= 1){
        reso = 1.f - 1E-20;
//        reso = 0.98;
        x->s_res = reso;
    }else if(reso<0.){
        reso = 0.f;
        x->s_res = reso;
    }else{
        x->s_res = reso;
    }
    
    mookVCF_calc(x);
    return 0;
}

void mookVCF_perform64(t_mookVCF *x,t_object *dsp64,double **ins,long numins,double **outs,long numouts,long sampleframes,long flags,void  *userparam){
    
    t_double *in1=ins[0];
    t_double *out=outs[0];
    t_double freq = x->s_fcon ? *ins[1] : x->s_freq; // vérifier s'il y a du signal dans les entrées 2 et 3
    t_double res = x->s_res ? *ins[2] : x->s_res;
    
    double X;
    double y1=x->s_ym1;
    double y2=x->s_ym2;
    double y3=x->s_ym3;
    double y4=x->s_ym4;
    double k = x->s_k;
    double p = x->s_p;
    double resterm = x->s_resterm;
    double t1 = x->s_t1;
    double t2 = x->s_t2;
    
    // scale resonance
    if(res >= 1){
        res = 1.f - 1E-20;
    }else if(res<0.){
        res = 0.f;
    }
    
    // do we need to calculate the coefs ?
    if(freq != x->s_freq || res != x->s_res){
        if(res != x->s_res){
            resterm = x->s_resterm = x->s_res * (t2+6.f*t1) / (t2-6.f*t1);
        }else{
            resterm = x->s_resterm;
        }
        if(freq !=x->s_freq){
            x->s_fqterm = (x->s_freq + x->s_freq) / x->s_sr;
            x->s_p = p = x->s_fqterm * (1.0f - 0.8f * x->s_fqterm);
            x->s_k = k = p + p - 1.f;
            x->s_t1 = (1.f - p) * 1.386249;
            t1 = x->s_t1;
            x->s_t2 = 12.f + t1 * t1;
            t2 = x->s_t2;
        }
    }

        
    while(sampleframes--){
    
        X = *in1++ - resterm * y4;
        y1 = X * p + x->s_lx * p - k * y1;
        y2 = y1 * p + x->s_ly1 * p - k * y2;
        y3 = y2 * p + x->s_ly2 * p - k * y3;
        y4 = y3 * p + x->s_ly3 * p - k * y4;

        y4 -= (y4*y4*y4) / 6.f;
    
        *out++=y4;
        x->s_lx = X; x->s_ly1 = y1; x->s_ly2 = y2; x->s_ly3 = y3;

    }
    
    x->s_ym1=y1; x->s_ym2=y2; x->s_ym3=y3; x->s_ym4=y4;
    
}

void mookVCF_clear(t_mookVCF *x){
    x->s_ym1=x->s_ym2=x->s_ym3=x->s_ym4=0.;
    x->s_p=x->s_k=x->s_lp=x->s_lk=0.;
    x->s_lx=x->s_ly1=x->s_ly2=x->s_ly3=0.;
}

void mookVCF_assist(t_mookVCF *x, void *b, long m, long a, char *s)
{
    if(m==2){
        sprintf(s, "(signal) Output");
    }else{
        switch (a) {
            case 0: sprintf(s, "(signal) Input"); break;
            case 1: sprintf(s, "(signal/float) Cutoff Frequency"); break;
            case 2: sprintf(s, "(signal/float) Resonance Control (0-1)"); break;
        }
    }
}

void mookVCF_calc(t_mookVCF *x){
    
    x->s_fqterm = (x->s_freq + x->s_freq) / x->s_sr;
    x->s_p = x->s_fqterm  * (1.0f - 0.8f * x->s_fqterm);
    x->s_k = x->s_p + x->s_p - 1.f;
    x->s_t1 = (1.f - x->s_p) * 1.386249;
    x->s_t2 = 12.f +  x->s_t1  *  x->s_t1 ;
    x->s_resterm = x->s_res * (x->s_t2+6.f* x->s_t1) / (x->s_t2-6.f* x->s_t1);
    
}

void *mookVCF_new(t_symbol *s, long argc, t_atom *argv){
    t_mookVCF *x = object_alloc(mookVCF_class);
    
    double freq, reso, offset;
    offset = attr_args_offset((short)argc, argv);
    dsp_setup((t_pxobject *)x, 3);
    
    if (offset) {
        freq = atom_getlong(argv);
        if ((argv)->a_type == A_LONG){
            object_post((t_object*)x, "arg[%ld] Cutoff Frequency: %ld", 0, atom_getlong(argv));
        }
        if (offset > 1){
            reso = atom_getfloat(argv + 1);
            if ((argv + 1)->a_type == A_FLOAT){
                object_post((t_object*)x, "arg[%ld] Resonance: %f", 1, atom_getfloat(argv+1));
            }
        }
    }
    
    x->s_freq = freq < 1. ? 1 : freq;
    
    if(reso >= 1){
        reso = 1.f - 1E-20;
        x->s_res = reso;
    }else if(reso<0.){
        reso = 0.f;
        x->s_res = reso;
    }else{
        x->s_res = reso;
    }
    
    attr_args_process(x, (short)argc, argv);
    
    x->s_sr = sys_getsr();
    x->s_lp = x->s_p;
    x->s_lk = x->s_k;
    
    mookVCF_calc(x);
    outlet_new((t_pxobject*)x,"signal");
    return x;
}


